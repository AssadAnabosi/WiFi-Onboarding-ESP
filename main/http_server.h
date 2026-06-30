#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTP server: register the REST API endpoints and the
 *        wildcard handler that serves the gzip-embedded web interface.
 */
esp_err_t http_server_start(void);

#ifdef __cplusplus
}
#endif
