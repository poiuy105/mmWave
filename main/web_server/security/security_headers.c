/**
 * @file security_headers.c
 * @brief HTTP security headers implementation
 */

#include "security_headers.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SECURITY_HEADERS";

const security_headers_config_t SECURITY_HEADERS_DEFAULT = {
    .x_content_type_options = true,
    .x_frame_options = true,
    .x_xss_protection = true,
    .strict_transport_security = false,  // Requires HTTPS
    .content_security_policy = false,    // Enable in production
    .csp_policy = NULL
};

static security_headers_config_t s_config;

void security_headers_init_default(void)
{
    s_config = SECURITY_HEADERS_DEFAULT;
    ESP_LOGI(TAG, "Security headers initialized with defaults");
}

const security_headers_config_t* security_headers_get_config(void)
{
    return &s_config;
}

void security_headers_set_config(const security_headers_config_t *config)
{
    if (config) {
        s_config = *config;
    }
}

void security_headers_set(httpd_req_t *req, const security_headers_config_t *config)
{
    if (req == NULL) {
        return;
    }

    if (config == NULL) {
        config = &s_config;
    }

    // X-Content-Type-Options: nosniff
    if (config->x_content_type_options) {
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    }

    // X-Frame-Options: DENY or SAMEORIGIN
    if (config->x_frame_options) {
        httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    }

    // X-XSS-Protection
    if (config->x_xss_protection) {
        httpd_resp_set_hdr(req, "X-XSS-Protection", "1; mode=block");
    }

    // Strict-Transport-Security (requires HTTPS support)
    if (config->strict_transport_security) {
        httpd_resp_set_hdr(req, "Strict-Transport-Security", "max-age=31536000; includeSubDomains");
    }

    // Content-Security-Policy
    if (config->content_security_policy && config->csp_policy) {
        httpd_resp_set_hdr(req, "Content-Security-Policy", config->csp_policy);
    }
}

void security_headers_set_cors(httpd_req_t *req, const char *allow_origin)
{
    if (req == NULL) {
        return;
    }

    // CORS headers
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");  // 24 hours

    if (allow_origin && strlen(allow_origin) > 0) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", allow_origin);

        // If allowed origin is not *, add Vary header
        if (strcmp(allow_origin, "*") != 0) {
            httpd_resp_set_hdr(req, "Vary", "Origin");
        }
    }
}
