#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dns_server_start(uint16_t port, const char *override_ip);
esp_err_t dns_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif
