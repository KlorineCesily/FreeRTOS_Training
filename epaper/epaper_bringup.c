#include <stdio.h>
#include <string.h>

#include "epaper_2in15g.h"
#include "pico/stdlib.h"

static uint8_t frame_buffer[EPAPER_2IN15G_BUFFER_SIZE];

static void set_pixel(uint x, uint y, epaper_2in15g_color_t color) {
    if ((x >= EPAPER_2IN15G_WIDTH) || (y >= EPAPER_2IN15G_HEIGHT)) {
        return;
    }

    const uint index = y * (EPAPER_2IN15G_WIDTH / EPAPER_2IN15G_PIXELS_PER_BYTE)
                       + (x / EPAPER_2IN15G_PIXELS_PER_BYTE);
    const uint shift = (EPAPER_2IN15G_PIXELS_PER_BYTE - 1 - (x % EPAPER_2IN15G_PIXELS_PER_BYTE)) * 2;
    const uint8_t mask = (uint8_t)(0x3u << shift);

    frame_buffer[index] = (uint8_t)((frame_buffer[index] & ~mask) | ((uint8_t)color << shift));
}

static void fill_screen(epaper_2in15g_color_t color) {
    memset(frame_buffer,
           epaper_2in15g_pack4(color, color, color, color),
           sizeof(frame_buffer));
}

static void fill_rect(uint x0, uint y0, uint width, uint height, epaper_2in15g_color_t color) {
    for (uint y = y0; y < (y0 + height); y++) {
        for (uint x = x0; x < (x0 + width); x++) {
            set_pixel(x, y, color);
        }
    }
}

static void draw_test_pattern(void) {
    fill_screen(EPAPER_2IN15G_WHITE);

    const uint band_height = EPAPER_2IN15G_HEIGHT / 4;
    fill_rect(0, 0, EPAPER_2IN15G_WIDTH, band_height, EPAPER_2IN15G_BLACK);
    fill_rect(0, band_height, EPAPER_2IN15G_WIDTH, band_height, EPAPER_2IN15G_WHITE);
    fill_rect(0, band_height * 2, EPAPER_2IN15G_WIDTH, band_height, EPAPER_2IN15G_YELLOW);
    fill_rect(0, band_height * 3, EPAPER_2IN15G_WIDTH, EPAPER_2IN15G_HEIGHT - band_height * 3, EPAPER_2IN15G_RED);

    for (uint y = 0; y < EPAPER_2IN15G_HEIGHT; y++) {
        set_pixel(0, y, EPAPER_2IN15G_BLACK);
        set_pixel(EPAPER_2IN15G_WIDTH - 1, y, EPAPER_2IN15G_BLACK);
    }

    for (uint x = 0; x < EPAPER_2IN15G_WIDTH; x++) {
        set_pixel(x, 0, EPAPER_2IN15G_BLACK);
        set_pixel(x, EPAPER_2IN15G_HEIGHT - 1, EPAPER_2IN15G_BLACK);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    printf("2.15inch e-Paper HAT+ (G) bring-up\r\n");
    printf("pins: spi=spi1 mosi=%d sck=%d cs=%d dc=%d rst=%d busy=%d pwr=%d\r\n",
           EPAPER_2IN15G_PIN_MOSI,
           EPAPER_2IN15G_PIN_SCK,
           EPAPER_2IN15G_PIN_CS,
           EPAPER_2IN15G_PIN_DC,
           EPAPER_2IN15G_PIN_RST,
           EPAPER_2IN15G_PIN_BUSY,
           EPAPER_2IN15G_PIN_PWR);
    printf("refresh is slow; this demo performs one full refresh then sleeps the panel\r\n");

    epaper_2in15g_init_io();
    printf("io init done, busy=%d\r\n", epaper_2in15g_busy_level() ? 1 : 0);

    printf("init panel...\r\n");
    if (!epaper_2in15g_init_panel()) {
        printf("panel init timeout, busy=%d; check BUSY pin, power, and HAT seating\r\n",
               epaper_2in15g_busy_level() ? 1 : 0);
        while (true) {
            sleep_ms(1000);
        }
    }
    printf("panel init done, busy=%d\r\n", epaper_2in15g_busy_level() ? 1 : 0);

    printf("draw test pattern...\r\n");
    draw_test_pattern();

    printf("display frame, please wait about 20 seconds...\r\n");
    if (!epaper_2in15g_display(frame_buffer)) {
        printf("display timeout, busy=%d; check BUSY pin and SPI wiring\r\n",
               epaper_2in15g_busy_level() ? 1 : 0);
        while (true) {
            sleep_ms(1000);
        }
    }
    printf("display command completed, busy=%d\r\n", epaper_2in15g_busy_level() ? 1 : 0);

    printf("keep panel powered for inspection before sleep...\r\n");
    sleep_ms(10000);
    printf("sleep panel\r\n");
    epaper_2in15g_sleep();

    while (true) {
        printf("done; reset the board to rerun the one-shot refresh demo\r\n");
        sleep_ms(10000);
    }
}
