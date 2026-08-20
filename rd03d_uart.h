#ifndef RD03D_UART_H
#define RD03D_UART_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t x;
    int32_t y;
    int32_t snelheid;

    uint32_t afstand;
    float hoek;

} radar_doel_t;

void rd03d_init(void);
void rd03d_update(void);
bool rd03d_has_new(void);
radar_doel_t* rd03d_get_doel(void);

#endif