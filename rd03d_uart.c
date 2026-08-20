#include "rd03d_uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "esp_log.h"

#include <string.h>
#include <math.h>

// =========================
// UART SETTINGS
// =========================

#define UART_NUM     UART_NUM_2
#define TX_PIN       GPIO_NUM_17
#define RX_PIN       GPIO_NUM_16
#define BAUD         256000

static const char *TAG = "RD03D";

// =========================
// RADAR COMMANDS
// =========================

static const uint8_t CMD_MULTI[] = {
    0xAA,0xFF,0x00,0x01,0x00,0x00,0x00,0x00,0x55,0xCC
};

static const uint8_t HEADER[] = {
    0xAA,0xFF,0x03,0x00
};

static const uint8_t TRAILER[] = {
    0x55,0xCC
};

// =========================
// VARIABLES
// =========================

static radar_doel_t doel = {0};

static bool new_data = false;

static uint8_t buf[256];
static int idx = 0;

static bool first_measurement = true;
static int32_t y_offset = 0;

// =========================
// PARSER
// =========================

static void parse(uint8_t *f, int len)
{
    if (len < 14) return;

    // check header
    if (memcmp(f, HEADER, 4) != 0) return;

    // check trailer
    if (memcmp(f + len - 2, TRAILER, 2) != 0) return;

    // =========================
    // RAW VALUES
    // =========================

    uint16_t raw_x = (uint16_t)(f[4] | (f[5] << 8));
    uint16_t raw_y = (uint16_t)(f[6] | (f[7] << 8));
    uint16_t raw_speed = (uint16_t)(f[8] | (f[9] << 8));

    // =========================
    // CONVERT SIGNED
    // =========================

    int32_t x;
    int32_t y;
    int32_t speed;

    // X
    if (raw_x > 32767)
        x = (int32_t)raw_x - 65536;
    else
        x = raw_x;

    // Y
    if (raw_y > 32767)
        y = (int32_t)raw_y - 65536;
    else
        y = raw_y;

    // SPEED
    if (raw_speed > 32767)
        speed = (int32_t)raw_speed - 65536;
    else
        speed = raw_speed;

    // ignore empty packet
    if (x == 0 && y == 0)
        return;

    // =========================
    // AUTO Y OFFSET
    // =========================

    if (first_measurement) {
        y_offset = -y;
        first_measurement = false;
    }

    // =========================
    // STORE VALUES
    // =========================

    doel.x = x;
    doel.y = y + y_offset;
    doel.snelheid = speed;

    // =========================
    // DISTANCE
    // =========================

    float xx = (float)doel.x;
    float yy = (float)doel.y;

    doel.afstand = (uint32_t)sqrtf((xx * xx) + (yy * yy));

    // =========================
    // ANGLE
    // =========================

    doel.hoek =
        atan2f(xx, yy) * 180.0f / M_PI;

    // =========================
    // VALID DATA
    // =========================

    new_data = true;
}

// =========================
// INIT
// =========================

void rd03d_init(void)
{
    uart_config_t cfg = {
        .baud_rate = BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(UART_NUM, &cfg);

    uart_set_pin(
        UART_NUM,
        TX_PIN,
        RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    uart_driver_install(
        UART_NUM,
        512,
        512,
        0,
        NULL,
        0
    );

    // zet radar in multi-target mode
    uart_write_bytes(
        UART_NUM,
        (const char*)CMD_MULTI,
        sizeof(CMD_MULTI)
    );

    ESP_LOGI(TAG, "RD03D gestart");
}

// =========================
// UPDATE
// =========================

void rd03d_update(void)
{
    uint8_t data[64];

    int len =
        uart_read_bytes(
            UART_NUM,
            data,
            sizeof(data),
            0
        );

    for (int i = 0; i < len; i++) {

        buf[idx++] = data[i];

        // frame gevonden
        if (
            idx >= 2 &&
            buf[idx - 2] == TRAILER[0] &&
            buf[idx - 1] == TRAILER[1]
        ) {
            parse(buf, idx);
            idx = 0;
        }

        // overflow protection
        if (idx >= sizeof(buf)) {
            idx = 0;
        }
    }
}

// =========================
// NEW DATA?
// =========================

bool rd03d_has_new(void)
{
    return new_data;
}

// =========================
// GET TARGET
// =========================

radar_doel_t* rd03d_get_doel(void)
{
    new_data = false;
    return &doel;
}