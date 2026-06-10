/*
 * SPDX-FileCopyrightText: 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * HLK-LD2460 24GHz Millimeter Wave Radar Driver
 *
 * Features:
 * - Binary protocol frame parsing with state machine
 * - Async UART event-driven architecture
 * - Full command set: report control, installation, range, baudrate, etc.
 * - esp_event based notification to application layer
 */

#ifdef CONFIG_RADAR_LD2460

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "radar_ld2460.h"

static const char *TAG = "ld2460";

/* ============ Event base definition ============ */
ESP_EVENT_DEFINE_BASE(ESP_LD2460_EVENT);

/* ============ Internal constants ============ */
#define LD2460_CMD_ACK_LEN         12   /* Fixed ACK frame length */
#define LD2460_REPORT_HEADER_LEN   4
#define LD2460_MAX_PAYLOAD_LEN     256
#define LD2460_UART_BUF_SIZE       1024
#define LD2460_EVENT_QUEUE_SIZE    16
#define LD2460_PARSER_STACK_SIZE   4096
#define LD2460_PARSER_PRIORITY     5

/* ============ Command function codes ============ */
#define LD2460_FUNC_REPORT_ENABLE  0x01
#define LD2460_FUNC_INSTALL_MODE   0x03
#define LD2460_FUNC_RANGE          0x04
#define LD2460_FUNC_SENSITIVITY    0x05
#define LD2460_FUNC_BAUDRATE       0x06
#define LD2460_FUNC_VERSION        0x07
#define LD2460_FUNC_RESTORE        0x08
#define LD2460_FUNC_REBOOT         0x09

/* ============ Frame structure ============ */
#define LD2460_FRAME_HEADER        0x55
#define LD2460_FRAME_FOOTER        0xAA

/* ============ ACK frame offsets ============ */
#define LD2460_ACK_HEADER          0
#define LD2460_ACK_FUNC            1
#define LD2460_ACK_STATUS          2
#define LD2460_ACK_LEN             3
#define LD2460_ACK_PAYLOAD         4

/* ============ Report frame offsets ============ */
#define LD2460_REPORT_HEADER       0
#define LD2460_REPORT_TYPE         1
#define LD2460_REPORT_LEN          2
#define LD2460_REPORT_PAYLOAD      4

/* ============ Internal types ============ */
typedef enum {
    LD2460_STATE_IDLE,
    LD2460_STATE_HEADER,
    LD2460_STATE_TYPE,
    LD2460_STATE_LEN,
    LD2460_STATE_PAYLOAD,
    LD2460_STATE_FOOTER,
} ld2460_parser_state_t;

typedef struct {
    ld2460_parser_state_t state;
    uint8_t type;
    uint16_t len;
    uint8_t payload[LD2460_MAX_PAYLOAD_LEN];
    uint16_t payload_pos;
} ld2460_parser_t;

typedef struct {
    uart_port_t uart_num;
    uint32_t baud_rate;
    QueueHandle_t event_queue;
    esp_event_loop_handle_t event_loop_hdl;
    TaskHandle_t parser_task;
    ld2460_parser_t parser;
    SemaphoreHandle_t cmd_mutex;
    SemaphoreHandle_t cmd_done;
    uint8_t cmd_ack[LD2460_CMD_ACK_LEN];
    bool cmd_pending;
    bool report_enabled;
} ld2460_context_t;

/* ============ Internal function declarations ============ */
static void ld2460_parser_task(void *arg);
static void ld2460_uart_event_task(void *arg);
static esp_err_t send_command(ld2460_context_t *ctx, uint8_t func, const uint8_t *payload, uint8_t payload_len);
static esp_err_t send_query_cmd(ld2460_context_t *ctx, uint8_t func, uint8_t *payload, uint8_t *payload_len);

/* ============ CRC calculation ============ */
static uint8_t ld2460_calc_crc(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc;
}

/* ============ UART event handler ============ */
static void IRAM_ATTR ld2460_uart_event_task(void *arg)
{
    ld2460_context_t *ctx = (ld2460_context_t *)arg;
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *)malloc(LD2460_UART_BUF_SIZE);

    while (1) {
        if (xQueueReceive(ctx->event_queue, &event, portMAX_DELAY)) {
            switch (event.type) {
            case UART_DATA:
                uart_read_bytes(ctx->uart_num, dtmp, event.size, portMAX_DELAY);
                for (size_t i = 0; i < event.size; i++) {
                    uint8_t ch = dtmp[i];
                    switch (ctx->parser.state) {
                    case LD2460_STATE_IDLE:
                        if (ch == LD2460_FRAME_HEADER) {
                            ctx->parser.state = LD2460_STATE_HEADER;
                        }
                        break;
                    case LD2460_STATE_HEADER:
                        ctx->parser.type = ch;
                        ctx->parser.state = LD2460_STATE_TYPE;
                        break;
                    case LD2460_STATE_TYPE:
                        ctx->parser.len = ch;
                        ctx->parser.state = LD2460_STATE_LEN;
                        break;
                    case LD2460_STATE_LEN:
                        ctx->parser.len |= (ch << 8);
                        ctx->parser.payload_pos = 0;
                        if (ctx->parser.len > LD2460_MAX_PAYLOAD_LEN) {
                            ctx->parser.state = LD2460_STATE_IDLE;
                        } else if (ctx->parser.len == 0) {
                            ctx->parser.state = LD2460_STATE_FOOTER;
                        } else {
                            ctx->parser.state = LD2460_STATE_PAYLOAD;
                        }
                        break;
                    case LD2460_STATE_PAYLOAD:
                        if (ctx->parser.payload_pos < ctx->parser.len) {
                            ctx->parser.payload[ctx->parser.payload_pos++] = ch;
                        }
                        if (ctx->parser.payload_pos >= ctx->parser.len) {
                            ctx->parser.state = LD2460_STATE_FOOTER;
                        }
                        break;
                    case LD2460_STATE_FOOTER:
                        if (ch == LD2460_FRAME_FOOTER) {
                            /* Valid frame received */
                            if (ctx->parser.type == 0x01) {
                                /* ACK frame */
                                if (ctx->cmd_pending && ctx->parser.len <= LD2460_CMD_ACK_LEN) {
                                    memcpy(ctx->cmd_ack, ctx->parser.payload, ctx->parser.len);
                                    xSemaphoreGive(ctx->cmd_done);
                                }
                            } else if (ctx->parser.type == 0x02) {
                                /* Report frame */
                                ld2460_report_t report;
                                memset(&report, 0, sizeof(report));
                                if (ctx->parser.len >= 4) {
                                    report.target_count = ctx->parser.payload[0];
                                    if (report.target_count > LD2460_MAX_TARGETS) {
                                        report.target_count = LD2460_MAX_TARGETS;
                                    }
                                    for (uint8_t j = 0; j < report.target_count; j++) {
                                        uint8_t offset = 1 + j * 8;
                                        if (offset + 7 < ctx->parser.len) {
                                            report.targets[j].id = ctx->parser.payload[offset];
                                            report.targets[j].x = (int16_t)(ctx->parser.payload[offset + 1] | (ctx->parser.payload[offset + 2] << 8));
                                            report.targets[j].y = (int16_t)(ctx->parser.payload[offset + 3] | (ctx->parser.payload[offset + 4] << 8));
                                            report.targets[j].z = (int16_t)(ctx->parser.payload[offset + 5] | (ctx->parser.payload[offset + 6] << 8));
                                            report.targets[j].confidence = ctx->parser.payload[offset + 7];
                                        }
                                    }
                                    esp_event_post_to(ctx->event_loop_hdl, ESP_LD2460_EVENT, LD2460_EVENT_REPORT, &report, sizeof(report), portMAX_DELAY);
                                }
                            }
                        }
                        ctx->parser.state = LD2460_STATE_IDLE;
                        break;
                    }
                }
                break;
            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "UART FIFO overflow");
                uart_flush_input(ctx->uart_num);
                xQueueReset(ctx->event_queue);
                break;
            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "UART buffer full");
                uart_flush_input(ctx->uart_num);
                xQueueReset(ctx->event_queue);
                break;
            case UART_BREAK:
                ESP_LOGW(TAG, "Rx Break");
                break;
            case UART_PARITY_ERR:
                ESP_LOGW(TAG, "UART parity error");
                break;
            case UART_FRAME_ERR:
                ESP_LOGW(TAG, "UART frame error");
                break;
            default:
                break;
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

/* ============ Command sending ============ */
static esp_err_t send_command(ld2460_context_t *ctx, uint8_t func, const uint8_t *payload, uint8_t payload_len)
{
    if (ctx == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t frame[LD2460_MAX_PAYLOAD_LEN + 6];
    uint16_t pos = 0;

    frame[pos++] = LD2460_FRAME_HEADER;
    frame[pos++] = func;
    frame[pos++] = payload_len & 0xFF;
    frame[pos++] = (payload_len >> 8) & 0xFF;
    for (uint8_t i = 0; i < payload_len; i++) {
        frame[pos++] = payload[i];
    }
    frame[pos++] = ld2460_calc_crc(frame + 1, pos - 1);
    frame[pos++] = LD2460_FRAME_FOOTER;

    xSemaphoreTake(ctx->cmd_mutex, portMAX_DELAY);
    ctx->cmd_pending = true;

    uart_write_bytes(ctx->uart_num, frame, pos);
    uart_wait_tx_done(ctx->uart_num, pdMS_TO_TICKS(100));

    if (xSemaphoreTake(ctx->cmd_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ctx->cmd_pending = false;
        xSemaphoreGive(ctx->cmd_mutex);
        return ESP_ERR_TIMEOUT;
    }

    ctx->cmd_pending = false;
    xSemaphoreGive(ctx->cmd_mutex);

    return ESP_OK;
}

static esp_err_t send_query_cmd(ld2460_context_t *ctx, uint8_t func, uint8_t *payload, uint8_t *payload_len)
{
    if (ctx == NULL || payload == NULL || payload_len == NULL) return ESP_ERR_INVALID_ARG;

    uint8_t query_payload[1] = {0x00};
    esp_err_t err = send_command(ctx, func, query_payload, 1);
    if (err != ESP_OK) return err;

    if (ctx->cmd_ack[LD2460_ACK_STATUS] != 0x00) {
        return ESP_FAIL;
    }

    uint8_t len = ctx->cmd_ack[LD2460_ACK_LEN];
    if (len > LD2460_MAX_PAYLOAD_LEN - LD2460_ACK_PAYLOAD) {
        len = LD2460_MAX_PAYLOAD_LEN - LD2460_ACK_PAYLOAD;
    }
    memcpy(payload, ctx->cmd_ack + LD2460_ACK_PAYLOAD, len);
    *payload_len = len;

    return ESP_OK;
}

/* ============ Public API ============ */
ld2460_handle_t ld2460_init(const ld2460_config_t *config)
{
    if (config == NULL) return NULL;

    ld2460_context_t *ctx = calloc(1, sizeof(ld2460_context_t));
    if (ctx == NULL) return NULL;

    ctx->uart_num = config->uart_num;
    ctx->baud_rate = config->baud_rate;

    /* Configure UART */
    uart_config_t uart_conf = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(config->uart_num, &uart_conf);
    if (err != ESP_OK) {
        free(ctx);
        return NULL;
    }

    err = uart_set_pin(config->uart_num, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        free(ctx);
        return NULL;
    }

    err = uart_driver_install(config->uart_num, LD2460_UART_BUF_SIZE, 0, LD2460_EVENT_QUEUE_SIZE, &ctx->event_queue, 0);
    if (err != ESP_OK) {
        free(ctx);
        return NULL;
    }

    /* Create event loop */
    esp_event_loop_args_t loop_args = {
        .queue_size = 16,
        .task_name = "ld2460_evt",
        .task_priority = 5,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY,
    };
    err = esp_event_loop_create(&loop_args, &ctx->event_loop_hdl);
    if (err != ESP_OK) {
        uart_driver_delete(config->uart_num);
        free(ctx);
        return NULL;
    }

    /* Create synchronization primitives */
    ctx->cmd_mutex = xSemaphoreCreateMutex();
    ctx->cmd_done = xSemaphoreCreateBinary();
    if (ctx->cmd_mutex == NULL || ctx->cmd_done == NULL) {
        esp_event_loop_delete(ctx->event_loop_hdl);
        uart_driver_delete(config->uart_num);
        free(ctx);
        return NULL;
    }

    /* Create parser task */
    xTaskCreate(ld2460_uart_event_task, "ld2460_parser", LD2460_PARSER_STACK_SIZE, ctx, LD2460_PARSER_PRIORITY, &ctx->parser_task);

    ESP_LOGI(TAG, "LD2460 driver initialized (UART%d, %lu baud, TX=GPIO%d, RX=GPIO%d)",
             config->uart_num, config->baud_rate, config->tx_pin, config->rx_pin);

    return ctx;
}

esp_err_t ld2460_deinit(ld2460_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;

    if (ctx->parser_task != NULL) {
        vTaskDelete(ctx->parser_task);
        ctx->parser_task = NULL;
    }

    if (ctx->event_loop_hdl != NULL) {
        esp_event_loop_delete(ctx->event_loop_hdl);
        ctx->event_loop_hdl = NULL;
    }

    if (ctx->cmd_mutex != NULL) {
        vSemaphoreDelete(ctx->cmd_mutex);
        ctx->cmd_mutex = NULL;
    }

    if (ctx->cmd_done != NULL) {
        vSemaphoreDelete(ctx->cmd_done);
        ctx->cmd_done = NULL;
    }

    if (ctx->event_queue != NULL) {
        uart_driver_delete(ctx->uart_num);
        ctx->event_queue = NULL;
    }

    free(ctx);
    return ESP_OK;
}

esp_err_t ld2460_enable_report(ld2460_handle_t handle, bool enable)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[1] = {enable ? 0x01 : 0x00};
    esp_err_t err = send_command(ctx, LD2460_FUNC_REPORT_ENABLE, payload, 1);
    if (err == ESP_OK) {
        ctx->report_enabled = enable;
    }
    return err;
}

esp_err_t ld2460_set_install_mode(ld2460_handle_t handle, ld2460_install_mode_t mode)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[1] = {(uint8_t)mode};
    return send_command(ctx, LD2460_FUNC_INSTALL_MODE, payload, 1);
}

esp_err_t ld2460_get_install_mode(ld2460_handle_t handle, ld2460_install_mode_t *mode)
{
    if (handle == NULL || mode == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[8];
    uint8_t payload_len = 0;
    esp_err_t err = send_query_cmd(ctx, LD2460_FUNC_INSTALL_MODE, payload, &payload_len);
    if (err == ESP_OK && payload_len >= 1) {
        *mode = (ld2460_install_mode_t)payload[0];
    }
    return err;
}

esp_err_t ld2460_set_detection_range(ld2460_handle_t handle, float distance, float angle_start, float angle_end)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[6];
    uint16_t dist = (uint16_t)(distance * 100);
    int16_t start = (int16_t)(angle_start * 100);
    int16_t end = (int16_t)(angle_end * 100);
    payload[0] = dist & 0xFF;
    payload[1] = (dist >> 8) & 0xFF;
    payload[2] = start & 0xFF;
    payload[3] = (start >> 8) & 0xFF;
    payload[4] = end & 0xFF;
    payload[5] = (end >> 8) & 0xFF;
    return send_command(ctx, LD2460_FUNC_RANGE, payload, 6);
}

esp_err_t ld2460_get_detection_range(ld2460_handle_t handle, ld2460_range_t *range)
{
    if (handle == NULL || range == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[8];
    uint8_t payload_len = 0;
    esp_err_t err = send_query_cmd(ctx, LD2460_FUNC_RANGE, payload, &payload_len);
    if (err == ESP_OK && payload_len >= 6) {
        range->distance = (float)((payload[0] | (payload[1] << 8))) / 100.0f;
        range->angle_start = (float)((int16_t)(payload[2] | (payload[3] << 8))) / 100.0f;
        range->angle_end = (float)((int16_t)(payload[4] | (payload[5] << 8))) / 100.0f;
    }
    return err;
}

esp_err_t ld2460_set_sensitivity(ld2460_handle_t handle, ld2460_sensitivity_t level)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[1] = {(uint8_t)level};
    return send_command(ctx, LD2460_FUNC_SENSITIVITY, payload, 1);
}

esp_err_t ld2460_get_sensitivity(ld2460_handle_t handle, ld2460_sensitivity_t *level)
{
    if (handle == NULL || level == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[8];
    uint8_t payload_len = 0;
    esp_err_t err = send_query_cmd(ctx, LD2460_FUNC_SENSITIVITY, payload, &payload_len);
    if (err == ESP_OK && payload_len >= 1 && level != NULL) {
        *level = (ld2460_sensitivity_t)payload[0];
    }
    return err;
}

esp_err_t ld2460_get_firmware_version(ld2460_handle_t handle, ld2460_version_t *version)
{
    if (handle == NULL || version == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    uint8_t payload[8];
    uint8_t payload_len = 0;
    esp_err_t err = send_query_cmd(ctx, LD2460_FUNC_VERSION, payload, &payload_len);
    if (err == ESP_OK && payload_len >= 4) {
        version->version_major = payload[0];
        version->version_minor = payload[1];
        version->year = payload[2];
        version->month = payload[3];
        version->mode = payload[4];
    }
    return err;
}

esp_err_t ld2460_restore_factory(ld2460_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    return send_command(ctx, LD2460_FUNC_RESTORE, NULL, 0);
}

esp_err_t ld2460_reboot(ld2460_handle_t handle)
{
    if (handle == NULL) return ESP_ERR_INVALID_ARG;
    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    return send_command(ctx, LD2460_FUNC_REBOOT, NULL, 0);
}

esp_err_t ld2460_register_event_callback(ld2460_handle_t handle, esp_event_handler_t event_handler, void *handler_args)
{
    if (handle == NULL || event_handler == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    return esp_event_handler_register_with(ctx->event_loop_hdl, ESP_LD2460_EVENT, ESP_EVENT_ANY_ID, event_handler, handler_args);
}

esp_err_t ld2460_unregister_event_callback(ld2460_handle_t handle, esp_event_handler_t event_handler)
{
    if (handle == NULL || event_handler == NULL) return ESP_ERR_INVALID_ARG;

    ld2460_context_t *ctx = (ld2460_context_t *)handle;
    return esp_event_handler_unregister_with(ctx->event_loop_hdl, ESP_LD2460_EVENT, ESP_EVENT_ANY_ID, event_handler);
}

#endif /* CONFIG_RADAR_LD2460 */
