/*
 * =========================================================
 * RD-03D + ESP32-C3 COMPLETE MAIN
 * =========================================================
 *
 * Aansluitingen:
 *
 * RD-03D TX  -> GPIO20 (ESP RX)
 * RD-03D RX  -> GPIO21 (ESP TX)
 * RD-03D 5V  -> 5V
 * RD-03D GND -> GND
 *
 * =========================================================
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "rd03d_uart.h"

// =========================================================
// WIFI SETTINGS
// =========================================================

#define WIFI_SSID "iPhone Louis"
#define WIFI_PASS "g027yea6"

// =========================================================
// VARIABLES
// =========================================================

static const char *TAG = "mmWave_SERVER";

static httpd_handle_t server = NULL;

// web variables
int getPosition = 0;

// detectie
int p1 = 0;
int p2 = 0;
int pos = 0;
int nt = 0;

int detect = 0;
int inBox = 0;

// box grootte in mm
int doos = 100;

// opgeslagen positie
static int32_t opgeslagen_x = 0;
static int32_t opgeslagen_y = 0;

// =========================================================
// SEND TO OTHER ESP
// =========================================================

void send_to_Rolluik_esp(const char *Rolluik){
    char url[256];

    sprintf(url,"http://172.20.10.8/set?%s", Rolluik);  //ip van de rolluik telkens instellen

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    esp_err_t err =
        esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI("ESP1", "Data verzonden");
    } else {
        ESP_LOGI("ESP1", "Verzenden mislukt");
    }

    esp_http_client_cleanup(client);
}

// ======================
// HTTP MMWAVE HANDLER
// ======================

static esp_err_t sendmmWave_handler(httpd_req_t *req){
    char buf[256] = {0};

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        if (strstr(buf, "getPosition=1")) {
            ESP_LOGI(TAG, "getPosition=1");
            getPosition = 1; 
        }

        httpd_resp_send(req, "OK", 2);
        return ESP_OK;
    }
    return ESP_OK;
}

// ======================
// START WEBSERVER
// ======================

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK) {

        httpd_uri_t sendmmWave = {
            .uri = "/sendmmWave",
            .method = HTTP_GET,
            .handler = sendmmWave_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(server, &sendmmWave);

        ESP_LOGI(TAG, "Webserver gestart op poort 80");
    }

    return server;
}

// ======================
// WIFI EVENT HANDLER
// ======================

static void event_handler(void *arg,esp_event_base_t event_base,int32_t event_id,void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        ESP_LOGI(TAG, "WiFi gestart -> verbinden...");
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {

        ESP_LOGI(TAG, "WiFi disconnected -> opnieuw verbinden...");
        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *) event_data;

        ESP_LOGI(TAG,
                 "ESP2 IP adres: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        // server pas starten NA wifi connectie
        if (server == NULL) {
            start_webserver();
        }
    }
}

// ======================
// WIFI INIT
// ======================

static void wifi_init(void)
{
    ESP_LOGI(TAG, "WiFi initialiseren...");

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    esp_wifi_init(&cfg);

    esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &event_handler,
        NULL
    );

    esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &event_handler,
        NULL
    );

    wifi_config_t wifi_config = {0};

    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);

    wifi_config.sta.threshold.authmode =
        WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    // start wifi
    esp_wifi_start();

    // extra zekerheid
    esp_wifi_connect();
}
// =========================================================
// MAIN
// =========================================================

void app_main(void)
{
    // =====================================================
    // NVS
    // =====================================================

    esp_err_t ret = nvs_flash_init();

    if (
        ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND
    ) {

        nvs_flash_erase();
        nvs_flash_init();
    }

    // =====================================================
    // WIFI
    // =====================================================

    wifi_init();

    vTaskDelay(pdMS_TO_TICKS(3000));

    // =====================================================
    // RADAR
    // =====================================================

    rd03d_init();

    ESP_LOGI(TAG, "RD03D gestart");

    // =====================================================
    // LOOP
    // =====================================================

    while (1) {

        rd03d_update();

        p2 = p1;

        // nieuwe data?
        if (rd03d_has_new()) {

            radar_doel_t *d =
                rd03d_get_doel();

            bool has_target =
                !(d->x == 0 && d->y == 0);

            // =============================================
            // POSITIE OPSLAAN
            // =============================================

            if(d->x < 0){
                d->x = 0;
            }

            if (getPosition && has_target) {

                opgeslagen_x = d->x;
                opgeslagen_y = d->y;

                pos = 1;
                getPosition = 0;

                ESP_LOGI(
                    TAG,
                    "Positie opgeslagen: X=%ld Y=%ld",
                    opgeslagen_x,
                    opgeslagen_y
                );

                vTaskDelay(pdMS_TO_TICKS(200));
            }


            // =============================================
            // PRINT
            // =============================================

            printf(
                "\n"
                "X=%ldmm "
                "Y=%ldmm | "
                "%.0f° | "
                "%ldcm | "
                "%+ldcm/s\n",

                d->x,
                d->y,
                d->hoek,
                d->afstand / 10,
                d->snelheid
            );

            // =============================================
            // BOX DETECTIE
            // =============================================

            if (
                (d->x >= (opgeslagen_x - doos)) &&
                (d->x <= (opgeslagen_x + doos)) &&
                (d->y >= (opgeslagen_y - doos)) &&
                (d->y <= (opgeslagen_y + doos))
            ) {

                inBox = 1;
                p1 = 1;
            }
            else {

                inBox = 0;
                p1 = 0;
            }

            // =============================================
            // CHANGE DETECT
            // =============================================

            if (p1 != p2) {
                // ignore empty packet
                if (d->x == 0 && d->y == 0)
                    return;   
                // target gevonden
                if (inBox || has_target) {

                    ESP_LOGI(TAG, "DETECT = 1");

                    send_to_Rolluik_esp(
                        "detect=1"
                    );
                }

                // target weg
                else {

                    ESP_LOGI(TAG, "DETECT = 0");

                    send_to_Rolluik_esp(
                        "detect=0"
                    );
                }
            }

            if (((pos == 0) && has_target == 1) && (nt == 0)) {
                ESP_LOGI(TAG, "DETECT = 1");
                send_to_Rolluik_esp("detect=1");
                nt = 1;
            }
            if (((pos == 0) && has_target == 0) && (nt == 1)) {
                ESP_LOGI(TAG, "DETECT = 0");
                send_to_Rolluik_esp("detect=0");
                nt = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}