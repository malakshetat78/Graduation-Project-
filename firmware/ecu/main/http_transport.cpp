#include "http_transport.h"

#include <cstdio>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "sdkconfig.h"

int EspHttpTransport::post_report(const char* line, size_t len) {
    esp_http_client_config_t cfg = {};
    cfg.url = CONFIG_ICVSP_REPORT_URL;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 10000;
    if (std::strncmp(CONFIG_ICVSP_REPORT_URL, "https://", 8) == 0) cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == nullptr) return 0;
    esp_http_client_set_header(c, "Content-Type", "application/json");

    int status = 0;
    bool not_an_ack = false;
    char body[512] = {};
    const esp_err_t err = esp_http_client_open(c, static_cast<int>(len));
    if (err == ESP_OK && esp_http_client_write(c, line, static_cast<int>(len)) == static_cast<int>(len) &&
        esp_http_client_fetch_headers(c) >= 0) {
        status = esp_http_client_get_status_code(c);
        const int n = esp_http_client_read_response(c, body, sizeof body - 1);
        body[n > 0 ? n : 0] = '\0';
        if ((status == 200 || status == 201) && !icvsp::Uplink::is_stored_ack(line, body)) {
            std::printf("HTTP POST %s -> %d but not a stored ACK for this report: kept, will retry\n",
                        CONFIG_ICVSP_REPORT_URL, status);
            status = 0;  // no valid answer: the report stays queued
            not_an_ack = true;
        }
    }
    // The backend's answer holds the event and device IDs and the status, never the position.
    if (status != 0)
        std::printf("HTTP POST %s -> %d %s\n", CONFIG_ICVSP_REPORT_URL, status, body);
    else if (err != ESP_OK)
        std::printf("HTTP POST %s -> no answer (%s)\n", CONFIG_ICVSP_REPORT_URL, esp_err_to_name(err));
    else if (!not_an_ack)
        std::printf("HTTP POST %s -> no answer\n", CONFIG_ICVSP_REPORT_URL);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return status;
}
