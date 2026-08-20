#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"

#define SW         17
#define ZC_PIN     18
#define TRIAC_PIN  19

#define EXAMPLE_ESP_WIFI_SSID      "telenet-E78BD"
#define EXAMPLE_ESP_WIFI_PASS      "ee4dbhBbmcC7"


static const char *TAG = "TRIAC_DIMMER";

static httpd_handle_t server = NULL;

// =====================
// DIM VALUE (0–100)
// =====================
static volatile uint16_t dim_value = 0;

// =====================
// TIMING STATE
// =====================
static volatile uint16_t tick = 0;
static volatile bool half_cycle = false;

// =====================
// MEASUREMENTS
// =====================
static volatile int64_t last_zc_time = 0;
static volatile int64_t cycle_time_us = 0;
static volatile int64_t time_to_fire_us = 0;

// =====================
// ZERO CROSS FILTER
// =====================
#define MIN_ZC_INTERVAL_US 8000

static esp_err_t lamp1_handler(httpd_req_t *req)
{
    char buf[100] = {0};

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK)
    {
        if (strstr(buf, "BrLamp1="))
        {
            char value[10];

            if (httpd_query_key_value(buf, "BrLamp1", value, sizeof(value)) == ESP_OK)
            {
                dim_value = atoi(value);
            }
        }

        if (strstr(buf, "hallo"))
        {
            printf("halllo\n");

            
        }
    }

    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "Lamp1 server werkt!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler
        };

        httpd_register_uri_handler(server, &root);

        httpd_uri_t lamp1 = {
            .uri = "/sendLamp1",
            .method = HTTP_GET,
            .handler = lamp1_handler
        };

        httpd_register_uri_handler(server, &lamp1);
    }

    return server;
}



// WiFi events
static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "📡 WiFi gestart, verbinden...");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {   
        ESP_LOGI(TAG, "❌ WiFi disconnected, opnieuw verbinden...");
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

        ESP_LOGI(TAG, "🌐 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "📱 Open browser: http://" IPSTR, IP2STR(&event->ip_info.ip));

        // 👉 Start webserver hier (BELANGRIJK)
        if (server == NULL) {
            server = start_webserver();
        }
    }
}

// WiFi
static void wifi_init(void) {
    ESP_LOGI(TAG, "📡 Verbinden met WiFi...");

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_config_t wifi_config = { 0 };
    strcpy((char*)wifi_config.sta.ssid, EXAMPLE_ESP_WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, EXAMPLE_ESP_WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // Extra zekerheid
    esp_wifi_connect();
}

// =====================
// ZERO CROSS ISR
// =====================
static void IRAM_ATTR zc_isr(void *arg)
{
    int64_t now = esp_timer_get_time();

    // filter ruis / dubbele pulses
    if (now - last_zc_time < MIN_ZC_INTERVAL_US)
    {
        return;
    }

    // tijd tussen zero-crosses (~10ms)
    cycle_time_us = now - last_zc_time;
    last_zc_time = now;

    tick = 0;
    half_cycle = true;

    gpio_set_level(TRIAC_PIN, 0);
}

// =====================
// TIMER ISR (50 µs)
// =====================
static bool IRAM_ATTR timer_cb(gptimer_handle_t timer,const gptimer_alarm_event_data_t *edata,void *user_data)
{
    if (!half_cycle) return false;

    tick++;

    // =========================
    // UIT-MARGE (ENIGE SAFETY)
    // =========================
    
        // altijd uit, nooit triggeren
        if (tick >= 175)
        {
            half_cycle = false;
        }

    // =========================
    // NORMALE WERKING
    // =========================
    uint16_t fire_tick = 175 - (dim_value/2);

    if (fire_tick > 199)
    {
        fire_tick = 199;
    }

    if (tick == fire_tick)
    {
        time_to_fire_us = tick * 50;
        gpio_set_level(TRIAC_PIN, 1);
    }

    if (tick == fire_tick + 5)
    {
        gpio_set_level(TRIAC_PIN, 0);
    }

    if (tick > 200)
    {
        half_cycle = false;
    }

    return false;
}

// =====================
// GPIO INIT
// =====================
static void gpio_init(void)
{
    gpio_set_direction(SW, GPIO_MODE_INPUT);
    gpio_set_direction(TRIAC_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TRIAC_PIN, 0);

    gpio_set_direction(ZC_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ZC_PIN, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(ZC_PIN, GPIO_INTR_NEGEDGE);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ZC_PIN, zc_isr, NULL);
}

// =====================
// TIMER INIT (50 µs)
// =====================
static void timer_init(void)
{
    gptimer_handle_t timer = NULL;

    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000 // 1 µs
    };

    gptimer_new_timer(&config, &timer);

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_cb
    };

    gptimer_register_event_callbacks(timer, &cbs, NULL);

    gptimer_enable(timer);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 50,   // 50 µs tick
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true
    };

    gptimer_set_alarm_action(timer, &alarm_config);
    gptimer_start(timer);
}

// =====================
// MAIN
// =====================
void app_main(void)
{

    gpio_init();
    timer_init();

    nvs_flash_init();

    wifi_init();

    vTaskDelay(5000 / portTICK_PERIOD_MS);


    ESP_LOGI(TAG, "Triac dimmer test gestart");

    while (1)
    {
        if (gpio_get_level(SW) == 0 && dim_value < 100)
        {
                dim_value++;
        }
        if (gpio_get_level(SW) == 1 && dim_value >= 100){
                    dim_value = 0;
        }


            ESP_LOGI(TAG,"DIM = %d", dim_value);

            vTaskDelay(pdMS_TO_TICKS(50));
    }
}