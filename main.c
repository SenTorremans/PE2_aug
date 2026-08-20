#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "esp_http_client.h"
#include "WEB_PAGE.h"

#define EXAMPLE_ESP_WIFI_SSID      "telenet-E78BD"
#define EXAMPLE_ESP_WIFI_PASS      "ee4dbhBbmcC7"

// Definieer de GPIO pin voor de LED
#define SW_up_GPIO 5
#define SW_down_GPIO 17
#define btn_up_GPIO 18
#define btn_down_GPIO 19

#define M_1_GPIO 22
#define M_2_GPIO 23
#define M_3_GPIO 32
#define M_4_GPIO 33

#define DETECT_DELAY 1000 //10sec

char rolluikIP[16] = "192.168.0.129";
char lamp1IP[16]   = "192.168.0.246";
char lamp2IP[16]   = "192.168.1.247";
char mmWaveIP[16]  = "192.168.1.103";

int keep_distance = 300; //3sec

char rolluikStatus[10] = "OPEN";

//custom variable
bool btn_up_falling_edge = 0;
bool prevState_up = 1;
bool currentState_up = 1;

bool btn_down_falling_edge = 0;
bool prevState_down = 1;
bool currentState_down = 1;

bool rolluikEnable = 1;
bool keep = 0;
int distance = 0;

int lastTimerMinute = -1;
static bool time_synced = false;

bool ochtendUp = 0;
bool avondDown = 0;
int openHour = 8;
int openMinute = 0;
int closeHour = 22;
int closeMinute = 0;
int keepHour = 7;
int keepMinute = 50;

bool lamp1En = 1;
bool lamp2En = 1;
bool lamp1PreOn = 0;
bool lamp2PreOn = 0;

bool setPosEnable = 1;
int detect = 0;
bool inBox = 0;
int lamp1Val = 0;
int lamp2Val = 0;
int detect_cnt = 0;

enum SetposRolluik{
  Op,
  Di,
  Ke
};

enum SetposRolluik rolluik = Op;

enum movement {
  idle,
  up,
  down
};

enum movement motor = idle;

time_t now;
struct tm timeinfo;


static const char *TAG = "HTTP_CTRL";

static httpd_handle_t server = NULL;

// Forward declaration
static httpd_handle_t start_webserver(void);

void time_sync_notification_cb(struct timeval *tv)
{
    time_synced = true;
    ESP_LOGI(TAG, "⏰ Time synced!");
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

//doorsturen naar Lamp1
void send_to_Lamp1_esp(const char *Lamp1)
{
    char url[256];

    snprintf(url,sizeof(url),"http://%s/sendLamp1?%s",lamp1IP,Lamp1);

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

//doorsturen naar Lamp2
void send_to_Lamp2_esp(const char *Lamp2)
{
    char url[256];

    snprintf(url,sizeof(url),"http://%s/sendLamp2?%s",lamp2IP,Lamp2);

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

//doorsturen naar mmWave
void send_to_mmWave_esp(const char *mmWave)
{
    char url[256];

    snprintf(url,sizeof(url),"http://%s/sendmmWave?%s",mmWaveIP,mmWave);

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

// Handler voor /set
static esp_err_t html_handler(httpd_req_t *req) {
    char buf[256] = {0};

    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
       // =====================
        // ROLLUIK ENABLE
        // =====================
        if (strstr(buf, "rolluikEnable=1")) {
            ESP_LOGI(TAG, "rolluikEnable=1!");
            rolluikEnable = 1;
        }

        if (strstr(buf, "rolluikEnable=0")) {
            ESP_LOGI(TAG, "rolluikEnable=0!");
            rolluikEnable = 0;
        }

        // =====================
        // ROLLUIK ACTIE
        // =====================
        if (strstr(buf, "rolluikAction=open")) {
            ESP_LOGI(TAG, "rolluik open!");
            keep = 0;
            distance = 0;
            if(motor == idle && gpio_get_level(SW_up_GPIO) == 1){
                gpio_set_level(M_1_GPIO, 0);
                gpio_set_level(M_2_GPIO, 0);
                gpio_set_level(M_3_GPIO, 1);
                gpio_set_level(M_4_GPIO, 1);
                vTaskDelay(100 / portTICK_PERIOD_MS);  
                motor = up;
            }
            else if(motor == down){
                gpio_set_level(M_1_GPIO, 0);
                gpio_set_level(M_2_GPIO, 0);
                gpio_set_level(M_3_GPIO, 1);
                gpio_set_level(M_4_GPIO, 1);
                vTaskDelay(100 / portTICK_PERIOD_MS);  
                motor = idle;
            }
        }

        if (strstr(buf, "rolluikAction=dicht")) {
            ESP_LOGI(TAG, "rolluik dicht!");
            keep = 0;
            distance = 0;
            if(motor == idle && gpio_get_level(SW_down_GPIO) == 1){
                gpio_set_level(M_1_GPIO, 0);
                gpio_set_level(M_2_GPIO, 0);
                gpio_set_level(M_3_GPIO, 1);
                gpio_set_level(M_4_GPIO, 1);
                vTaskDelay(10 / portTICK_PERIOD_MS);  
                motor = down;
            }
            else if(motor == up){
                gpio_set_level(M_1_GPIO, 0);
                gpio_set_level(M_2_GPIO, 0);
                gpio_set_level(M_3_GPIO, 1);
                gpio_set_level(M_4_GPIO, 1);
                vTaskDelay(100 / portTICK_PERIOD_MS);  
                motor = idle;
            }
        }

        if (strstr(buf, "rolluikAction=keep")) {
            ESP_LOGI(TAG, "rolluik keep!");
            gpio_set_level(M_1_GPIO, 0);
            gpio_set_level(M_2_GPIO, 0);
            gpio_set_level(M_3_GPIO, 1);
            gpio_set_level(M_4_GPIO, 1);
            vTaskDelay(10 / portTICK_PERIOD_MS);
            distance = 0;
            keep = 1;
            
        }

         if (strstr(buf, "keepDistance=")) {
            char keepDistanceValue[10];

            if (httpd_query_key_value(buf, "keepDistance",keepDistanceValue,sizeof(keepDistanceValue)) == ESP_OK) {

                keep_distance = atoi(keepDistanceValue);
            }
        }

        // =====================
        // ZON FUNCTIES
        // =====================
        if (strstr(buf, "rolluikSunUp=1")) {
            ESP_LOGI(TAG, "SunUp enabled!");
            ochtendUp = 1;
        }

        if (strstr(buf, "rolluikSunUp=0")) {
            ESP_LOGI(TAG, "SunUp disabled!");
            ochtendUp = 0;
        }

        if (strstr(buf, "rolluikSunDown=1")) {
            ESP_LOGI(TAG, "SunDown enabled!");
            avondDown = 1;
        }

        if (strstr(buf, "rolluikSunDown=0")) {
            ESP_LOGI(TAG, "SunDown disabled!");
            avondDown = 0;
        }

        // =====================
        // TIJDEN
        // =====================
        if (strstr(buf, "rolluikTimeOpen=")) {
            char timeOpenStr[10];

            if (httpd_query_key_value(buf, "rolluikTimeOpen", timeOpenStr, sizeof(timeOpenStr)) == ESP_OK) {

                sscanf(timeOpenStr, "%d:%d", &openHour, &openMinute);

                ESP_LOGI(TAG, "Open tijd ontvangen: %02d:%02d", openHour, openMinute);
            }
        }

        if (strstr(buf, "rolluikTimeClose=")) {
            char timeCloseStr[10];

            if (httpd_query_key_value(buf, "rolluikTimeClose", timeCloseStr, sizeof(timeCloseStr)) == ESP_OK) {

                sscanf(timeCloseStr, "%d:%d", &closeHour, &closeMinute);

                ESP_LOGI(TAG, "Close tijd ontvangen: %02d:%02d", closeHour, closeMinute);
            }
        }

        if (strstr(buf, "rolluikTimeKeep=")) {
            char timeKeepStr[10];

            if (httpd_query_key_value(buf, "rolluikTimeKeep", timeKeepStr, sizeof(timeKeepStr)) == ESP_OK) {

                sscanf(timeKeepStr, "%d:%d", &keepHour, &keepMinute);

                ESP_LOGI(TAG, "Keep tijd ontvangen: %02d:%02d", keepHour, keepMinute);
            }
        }

        //website info doorsturen naar Lamp1
        if (strstr(buf, "EnLamp1=1")) {
             lamp1En = 1;
        }
        if (strstr(buf, "EnLamp1=0")) {
             lamp1En = 0;
        }
        if (strstr(buf, "BrLamp1=")) {
        char Lamp1Value[10];
        char sendBufferLamp1[32];

        if (httpd_query_key_value(buf, "BrLamp1", Lamp1Value, sizeof(Lamp1Value)) == ESP_OK) {

                snprintf(sendBufferLamp1, sizeof(sendBufferLamp1),
                        "BrLamp1=%s", Lamp1Value);

                send_to_Lamp1_esp(sendBufferLamp1);
            }
        }
        if (strstr(buf, "PreLamp1=PreOn1")) {
             lamp1PreOn = 1;
        }
        if (strstr(buf, "PreLamp1=PreOff1")) {
             lamp1PreOn = 0;
        }
   


    //website info doorsturen naar Lamp2
        if (strstr(buf, "EnLamp2=1")) {
             lamp2En = 1;
        }
        if (strstr(buf, "EnLamp2=0")) {
             lamp2En = 0;
        }
        if (strstr(buf, "BrLamp2=")) {
        char Lamp2Value[10];
        char sendBufferLamp2[32];

        if (httpd_query_key_value(buf, "BrLamp2", Lamp2Value, sizeof(Lamp2Value)) == ESP_OK) {

                snprintf(sendBufferLamp2, sizeof(sendBufferLamp2),
                        "BrLamp2=%s", Lamp2Value);

                send_to_Lamp2_esp(sendBufferLamp2);
            }
        }
        if (strstr(buf, "PreLamp2=PreOn2")) {
             lamp2PreOn = 1;
        }
        if (strstr(buf, "PreLamp2=PreOff2")) {
             lamp2PreOn = 0;
        }


        //website info doorsturen naar mmWave
        if (strstr(buf, "getPosition=1")) {
             send_to_mmWave_esp("getPosition=1");   
        }

        //info opslaan in variable om later te gebruiken
        if (strstr(buf, "setPosEnable=1")) {
             setPosEnable = 1;
        }
        if (strstr(buf, "setPosEnable=0")) {
             setPosEnable = 0;
        }
        if (strstr(buf, "SetposRolluik=open")) {
             rolluik = Op;
        }
        if (strstr(buf, "SetposRolluik=dicht")) {
             rolluik = Di;
        }
        if (strstr(buf, "SetposRolluik=keep")) {
             rolluik = Ke;
        }
        if (strstr(buf, "SetposLamp1=")) {
        char SetPosLamp1Value[10];
        char sendBufferPosLamp1[32];

            if (httpd_query_key_value(buf, "SetposLamp1", SetPosLamp1Value, sizeof(SetPosLamp1Value)) == ESP_OK) {

                snprintf(sendBufferPosLamp1, sizeof(sendBufferPosLamp1),"SetposLamp1=%s", SetPosLamp1Value);

                lamp1Val = atoi(SetPosLamp1Value);  // <-- hier
            }
        }
        if (strstr(buf, "SetposLamp2=")) {
        char SetPosLamp2Value[10];
        char sendBufferPosLamp2[32];

            if (httpd_query_key_value(buf, "SetposLamp2", SetPosLamp2Value, sizeof(SetPosLamp2Value)) == ESP_OK) {

                snprintf(sendBufferPosLamp2, sizeof(sendBufferPosLamp2),"SetposLamp2=%s", SetPosLamp2Value);

                lamp2Val = atoi(SetPosLamp2Value);  // <-- hier
            }
        }

        if (strstr(buf, "detect=1")) {
            ESP_LOGI(TAG, "detect=1!");
            detect = 1;
            detect_cnt = 0;
        }

        if (strstr(buf, "inBox=1")) {
            ESP_LOGI(TAG, "inBox=1!");
            inBox = 1;
        }
        if (strstr(buf, "inBox=0")) {
            ESP_LOGI(TAG, "inBox=0!");
            inBox = 0;
        }

        // =====================
        // IP ADRESSEN
        // =====================

        // ROLLUIK IP
        if (strstr(buf, "rolluikIP=")) {

            char ipValue[16];

            if (httpd_query_key_value(
                    buf,
                    "rolluikIP",
                    ipValue,
                    sizeof(ipValue)) == ESP_OK) {

                strncpy(
                    rolluikIP,
                    ipValue,
                    sizeof(rolluikIP) - 1
                );

                rolluikIP[sizeof(rolluikIP) - 1] = '\0';

                ESP_LOGI(TAG, "Rolluik IP: %s", rolluikIP);
            }
        }


        // LAMP 1 IP
        if (strstr(buf, "lamp1IP=")) {

            char ipValue[16];

            if (httpd_query_key_value(
                    buf,
                    "lamp1IP",
                    ipValue,
                    sizeof(ipValue)) == ESP_OK) {

                strncpy(
                    lamp1IP,
                    ipValue,
                    sizeof(lamp1IP) - 1
                );

                lamp1IP[sizeof(lamp1IP) - 1] = '\0';

                ESP_LOGI(TAG, "Lamp1 IP: %s", lamp1IP);
            }
        }


        // LAMP 2 IP
        if (strstr(buf, "lamp2IP=")) {

            char ipValue[16];

            if (httpd_query_key_value(
                    buf,
                    "lamp2IP",
                    ipValue,
                    sizeof(ipValue)) == ESP_OK) {

                strncpy(
                    lamp2IP,
                    ipValue,
                    sizeof(lamp2IP) - 1
                );

                lamp2IP[sizeof(lamp2IP) - 1] = '\0';

                ESP_LOGI(TAG, "Lamp2 IP: %s", lamp2IP);
            }
        }


        // MMWAVE IP
        if (strstr(buf, "mmWaveIP=")) {

            char ipValue[16];

            if (httpd_query_key_value(
                    buf,
                    "mmWaveIP",
                    ipValue,
                    sizeof(ipValue)) == ESP_OK) {

                strncpy(
                    mmWaveIP,
                    ipValue,
                    sizeof(mmWaveIP) - 1
                );

                mmWaveIP[sizeof(mmWaveIP) - 1] = '\0';

                ESP_LOGI(TAG, "mmWave IP: %s", mmWaveIP);
            }
        }
        
    }

    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}


// Root pagina
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, strlen(html_page));
}

static esp_err_t status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, rolluikStatus, strlen(rolluikStatus));

    return ESP_OK;
}

// Webserver
static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "🚀 Server gestart");

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t led = {
            .uri = "/set",
            .method = HTTP_GET,
            .handler = html_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &led);

        httpd_uri_t status = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(server, &status);
    }
    return server;
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


void M_idle(void)
    {
            //Motor idle
            gpio_set_level(M_1_GPIO, 0);
            gpio_set_level(M_2_GPIO, 0);
            gpio_set_level(M_3_GPIO, 1);
            gpio_set_level(M_4_GPIO, 1);

            printf("idle\n");
            strcpy(rolluikStatus, "IDLE");

        
        if(gpio_get_level(SW_up_GPIO) == 1 && btn_up_falling_edge == 1){    
            distance = 0;
            keep = 0;

            gpio_set_level(M_1_GPIO, 0);
            gpio_set_level(M_2_GPIO, 0);
            gpio_set_level(M_3_GPIO, 1);
            gpio_set_level(M_4_GPIO, 1);
            vTaskDelay(100 / portTICK_PERIOD_MS); 

            motor = up; 
        } 
        else{
                if(gpio_get_level(SW_down_GPIO) == 1 && btn_down_falling_edge == 1){   
                distance = 0; 
                keep = 0;
                
                gpio_set_level(M_1_GPIO, 0);
                gpio_set_level(M_2_GPIO, 0);
                gpio_set_level(M_3_GPIO, 1);
                gpio_set_level(M_4_GPIO, 1);
                vTaskDelay(100 / portTICK_PERIOD_MS);

                motor = down; 
            } 
        }
    }
        

    void M_up(void){
            //Motor up
            gpio_set_level(M_1_GPIO, 0);
            gpio_set_level(M_2_GPIO, 1);
            gpio_set_level(M_3_GPIO, 0);
            gpio_set_level(M_4_GPIO, 1);

            printf("up\n");


        if(gpio_get_level(SW_up_GPIO) == 0 || btn_down_falling_edge == 1){  
            distance = 0;
            keep = 0;

            gpio_set_level(M_1_GPIO, 0);
            gpio_set_level(M_2_GPIO, 0);
            gpio_set_level(M_3_GPIO, 1);
            gpio_set_level(M_4_GPIO, 1);
            vTaskDelay(100 / portTICK_PERIOD_MS);  

            motor = idle;  
        } 
        strcpy(rolluikStatus, "OPEN");  
    }

    void M_down(void){
        //Motor down
        gpio_set_level(M_1_GPIO, 1);
        gpio_set_level(M_2_GPIO, 0);
        gpio_set_level(M_3_GPIO, 1);
        gpio_set_level(M_4_GPIO, 0);

        printf("down\n");

        
        if(gpio_get_level(SW_down_GPIO) == 0 || btn_up_falling_edge == 1){ 
            distance = 0;
            keep = 0;

            gpio_set_level(M_1_GPIO, 0);
            gpio_set_level(M_2_GPIO, 0);
            gpio_set_level(M_3_GPIO, 1);
            gpio_set_level(M_4_GPIO, 1);
            vTaskDelay(100 / portTICK_PERIOD_MS);   

            motor = idle;  
        }   
        strcpy(rolluikStatus, "DICHT");
    }

    void SetPosHandler(void){
        if(setPosEnable == 1){
            detect_cnt++;
            if(detect_cnt >= DETECT_DELAY)
            {
                //send_to_Lamp1_esp("BrLamp1=0");
                //send_to_Lamp2_esp("BrLamp2=0");    
                 //ESP_LOGI(TAG, "alles uit");  
                detect = 0;
            }
            if(detect == 1){
                //send_to_Lamp1_esp("BrLamp1=100");
                //send_to_Lamp2_esp("BrLamp2=100");     
                 ESP_LOGI(TAG, "alles aan");           
            }
            if(inBox == 1){
                //rolluik actie
                switch (rolluik)
                {
                    case Op:
                        gpio_set_level(M_1_GPIO, 0);
                        gpio_set_level(M_2_GPIO, 0);
                        gpio_set_level(M_3_GPIO, 1);
                        gpio_set_level(M_4_GPIO, 1);
                        vTaskDelay(100 / portTICK_PERIOD_MS);   
                        motor = up;
                        break;
                    case Di:
                        gpio_set_level(M_1_GPIO, 0);
                        gpio_set_level(M_2_GPIO, 0);
                        gpio_set_level(M_3_GPIO, 1);
                        gpio_set_level(M_4_GPIO, 1);
                        vTaskDelay(100 / portTICK_PERIOD_MS);   
                        motor = down;
                        break;
                    case Ke:
                        gpio_set_level(M_1_GPIO, 0);
                        gpio_set_level(M_2_GPIO, 0);
                        gpio_set_level(M_3_GPIO, 1);
                        gpio_set_level(M_4_GPIO, 1);
                        vTaskDelay(10 / portTICK_PERIOD_MS);
                        keep = 1;
                    break;
                
                default:
                printf("rolluik is leeg");
                    break;
                }

                //lamp1 actie
                char BufferLamp1[32];
                snprintf(BufferLamp1, sizeof(BufferLamp1),"BrLamp1=%d", lamp1Val);
                send_to_Lamp1_esp(BufferLamp1);

                //lamp2 actie
                char BufferLamp2[32];
                snprintf(BufferLamp2, sizeof(BufferLamp2),"BrLamp2=%d", lamp2Val);
                send_to_Lamp2_esp(BufferLamp2);

                inBox = 0;
            }
            
        }
   }

    void lampPreOnHandler(void){
        if(detect == 1){
            if(lamp1PreOn == 1 && lamp1En == 1){
                send_to_Lamp1_esp("BrLamp1=100");
            }

            if(lamp2PreOn == 1 && lamp2En == 1){
                send_to_Lamp2_esp("BrLamp2=100");
            }
            detect = 2; //geen 0 of 1 dus in "idle state"
        }

        if(detect == 0){
            if(lamp1PreOn == 1 && lamp1En == 1){
                send_to_Lamp1_esp("BrLamp1=0");
            }
            if(lamp2PreOn == 1 && lamp2En == 1){
                send_to_Lamp2_esp("BrLamp2=0");
            }
            detect = 2;
        }
    }

    void btn_detection(void){
        //btn_up_detection
        currentState_up = gpio_get_level(btn_up_GPIO);
        if(currentState_up == 0 && prevState_up == 1){
            btn_up_falling_edge = 1;
            keep = 0;
            distance = 0;
        }
        else{
            btn_up_falling_edge = 0;
        }
        prevState_up = currentState_up;

        //btn_down_detection
        currentState_down = gpio_get_level(btn_down_GPIO);
        if(currentState_down == 0 && prevState_down == 1){
            btn_down_falling_edge = 1;
            keep = 0;
            distance = 0;
        }
        else{
            btn_down_falling_edge = 0;
        }
        prevState_down = currentState_down;
    }

    void timer(void){
        time(&now);
        localtime_r(&now, &timeinfo);

        if(rolluikEnable == 1){
            static time_t last_check = 0;

            if(now != last_check)
            {
                last_check = now;

                if(ochtendUp == 1 && timeinfo.tm_hour == keepHour && timeinfo.tm_min == keepMinute && keep == 0){
                    gpio_set_level(M_1_GPIO, 0);
                    gpio_set_level(M_2_GPIO, 0);
                    gpio_set_level(M_3_GPIO, 1);
                    gpio_set_level(M_4_GPIO, 1);
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    keep = 1;
                }

                if(ochtendUp == 1 && timeinfo.tm_hour == openHour && timeinfo.tm_min == openMinute && gpio_get_level(SW_up_GPIO) == 1){
                    keep = 0;
                    distance = 0;
                    gpio_set_level(M_1_GPIO, 0);
                    gpio_set_level(M_2_GPIO, 0);
                    gpio_set_level(M_3_GPIO, 1);
                    gpio_set_level(M_4_GPIO, 1);
                    vTaskDelay(100 / portTICK_PERIOD_MS); 
                    motor = up;
                }

                if(avondDown == 1 && timeinfo.tm_hour == closeHour && timeinfo.tm_min == closeMinute && gpio_get_level(SW_down_GPIO) == 1){
                    keep = 0;
                    distance = 0;
                    gpio_set_level(M_1_GPIO, 0);
                    gpio_set_level(M_2_GPIO, 0);
                    gpio_set_level(M_3_GPIO, 1);
                    gpio_set_level(M_4_GPIO, 1);
                    vTaskDelay(100 / portTICK_PERIOD_MS); 
                    motor = down; 
                }
            }
        }
    }

    void motorHandler(void){
        //juiste motor beweging
        if(keep == 1){ 
            if(gpio_get_level(SW_down_GPIO) == 1 && distance == 0){
                M_down();
            }
            else{
                if(distance == 0){
                    vTaskDelay(10 / portTICK_PERIOD_MS);  
                    gpio_set_level(M_1_GPIO, 0);
                    gpio_set_level(M_2_GPIO, 0);
                    gpio_set_level(M_3_GPIO, 1);
                    gpio_set_level(M_4_GPIO, 1);
                    vTaskDelay(10 / portTICK_PERIOD_MS);  
                }
        
                if(distance < keep_distance){
                    distance++;
                    M_up();
                }
                else{
                    keep = 0;
                    gpio_set_level(M_1_GPIO, 0);
                    gpio_set_level(M_2_GPIO, 0);
                    gpio_set_level(M_3_GPIO, 1);
                    gpio_set_level(M_4_GPIO, 1);
                    vTaskDelay(100 / portTICK_PERIOD_MS);  
                    motor = idle;
                }
            }
            strcpy(rolluikStatus, "KEEP");
        }
        else{
            switch (motor)
            {
            case idle:
                M_idle();
                break;

            case up:
                M_up();
                break;

            case down:
                M_down();
                break;
            
            default:
                ESP_LOGI(TAG, "ERROR!!!");
                M_idle();
                break;
            }
        }
    }
        
void app_main(void)
{
    // Configureer de GPIO pin als output

    gpio_set_direction(SW_up_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(SW_down_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(btn_up_GPIO, GPIO_MODE_INPUT);
    gpio_set_direction(btn_down_GPIO, GPIO_MODE_INPUT);

    gpio_set_direction(M_1_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(M_2_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(M_3_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(M_4_GPIO, GPIO_MODE_OUTPUT);

    //Motor init
    gpio_set_level(M_1_GPIO, 0);
    gpio_set_level(M_2_GPIO, 0);
    gpio_set_level(M_3_GPIO, 1);
    gpio_set_level(M_4_GPIO, 1);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    


    //website inits
    ESP_LOGI(TAG, "🎯 ESP32 TEST website");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }


    wifi_init();
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "✅ Klaar! Wachten op IP...");

    
    // Tijdserver starten
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    //juiste europeese tijd met winter en zomer uren
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // Wachten tot tijd binnen is
    while (!time_synced) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    

    
    // Oneindige loop
    while (1) {
        //tijd ophalen
        time(&now);
        localtime_r(&now, &timeinfo);
        
        timer();
        btn_detection();
        motorHandler();
        SetPosHandler();
        lampPreOnHandler();
        printf("%s\t%s\t%s\t%s\n" , rolluikIP , lamp1IP , lamp2IP , mmWaveIP);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}