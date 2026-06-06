#ifndef EPAPER_2IN15G_H
#define EPAPER_2IN15G_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"

enum {
    EPAPER_2IN15G_WIDTH = 160,
    EPAPER_2IN15G_HEIGHT = 296,
    EPAPER_2IN15G_PIXELS_PER_BYTE = 4,
    EPAPER_2IN15G_BUFFER_SIZE = ((EPAPER_2IN15G_WIDTH + 3) / 4) * EPAPER_2IN15G_HEIGHT,
};

typedef enum {
    EPAPER_2IN15G_BLACK = 0x0,
    EPAPER_2IN15G_WHITE = 0x1,
    EPAPER_2IN15G_YELLOW = 0x2,
    EPAPER_2IN15G_RED = 0x3,
} epaper_2in15g_color_t;

#ifndef EPAPER_2IN15G_SPI_PORT
#define EPAPER_2IN15G_SPI_PORT spi1
#endif

#ifndef EPAPER_2IN15G_BAUDRATE_HZ
#define EPAPER_2IN15G_BAUDRATE_HZ (4000 * 1000)
#endif

// Defaults follow the RP2350-PiZero 40-pin header and Waveshare HAT mapping.
#ifndef EPAPER_2IN15G_PIN_MOSI
#define EPAPER_2IN15G_PIN_MOSI 11
#endif

#ifndef EPAPER_2IN15G_PIN_SCK
#define EPAPER_2IN15G_PIN_SCK 10
#endif

#ifndef EPAPER_2IN15G_PIN_CS
#define EPAPER_2IN15G_PIN_CS 8
#endif

#ifndef EPAPER_2IN15G_PIN_DC
#define EPAPER_2IN15G_PIN_DC 25
#endif

#ifndef EPAPER_2IN15G_PIN_RST
#define EPAPER_2IN15G_PIN_RST 17
#endif

#ifndef EPAPER_2IN15G_PIN_BUSY
#define EPAPER_2IN15G_PIN_BUSY 24
#endif

#ifndef EPAPER_2IN15G_PIN_PWR
#define EPAPER_2IN15G_PIN_PWR 18
#endif

void epaper_2in15g_init_io(void);
bool epaper_2in15g_init_panel(void);
bool epaper_2in15g_clear(epaper_2in15g_color_t color);
bool epaper_2in15g_display(const uint8_t *image);
void epaper_2in15g_sleep(void);
bool epaper_2in15g_busy_level(void);

uint8_t epaper_2in15g_pack4(epaper_2in15g_color_t p0,
                            epaper_2in15g_color_t p1,
                            epaper_2in15g_color_t p2,
                            epaper_2in15g_color_t p3);

#endif
