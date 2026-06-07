#include "app_draw.h"

#include <stdbool.h>
#include <string.h>

#include "app_font.h"

enum {
    CARD_DISPLAY_WIDTH = EPAPER_2IN15G_HEIGHT,
    CARD_DISPLAY_HEIGHT = EPAPER_2IN15G_WIDTH,
};

static void set_display_pixel(uint8_t *frame_buffer,
                              uint32_t x,
                              uint32_t y,
                              epaper_2in15g_color_t color) {
    if ((x >= EPAPER_2IN15G_WIDTH) || (y >= EPAPER_2IN15G_HEIGHT)) {
        return;
    }

    const uint32_t stride = EPAPER_2IN15G_WIDTH / EPAPER_2IN15G_PIXELS_PER_BYTE;
    const uint32_t index = (y * stride) + (x / EPAPER_2IN15G_PIXELS_PER_BYTE);
    const uint32_t shift =
        (EPAPER_2IN15G_PIXELS_PER_BYTE - 1 - (x % EPAPER_2IN15G_PIXELS_PER_BYTE)) * 2;
    const uint8_t mask = (uint8_t)(0x3u << shift);

    frame_buffer[index] =
        (uint8_t)((frame_buffer[index] & ~mask) | ((uint8_t)color << shift));
}

void app_draw_fill(uint8_t *frame_buffer, epaper_2in15g_color_t color) {
    memset(frame_buffer,
           epaper_2in15g_pack4(color, color, color, color),
           EPAPER_2IN15G_BUFFER_SIZE);
}

static void fill_display_rect(uint8_t *frame_buffer,
                              uint32_t x0,
                              uint32_t y0,
                              uint32_t width,
                              uint32_t height,
                              epaper_2in15g_color_t color) {
    for (uint32_t y = y0; y < (y0 + height); y++) {
        for (uint32_t x = x0; x < (x0 + width); x++) {
            set_display_pixel(frame_buffer, x, y, color);
        }
    }
}

static void set_landscape_display_pixel(uint8_t *frame_buffer,
                                        uint32_t x,
                                        uint32_t y,
                                        epaper_2in15g_color_t color) {
    if ((x >= CARD_DISPLAY_WIDTH) || (y >= CARD_DISPLAY_HEIGHT)) {
        return;
    }

    set_display_pixel(frame_buffer, y, EPAPER_2IN15G_HEIGHT - 1 - x, color);
}

static void fill_landscape_display_rect(uint8_t *frame_buffer,
                                        uint32_t x0,
                                        uint32_t y0,
                                        uint32_t width,
                                        uint32_t height,
                                        epaper_2in15g_color_t color) {
    for (uint32_t y = y0; y < (y0 + height); y++) {
        for (uint32_t x = x0; x < (x0 + width); x++) {
            set_landscape_display_pixel(frame_buffer, x, y, color);
        }
    }
}

static void draw_landscape_display_rect_outline(uint8_t *frame_buffer,
                                                uint32_t x0,
                                                uint32_t y0,
                                                uint32_t width,
                                                uint32_t height,
                                                uint32_t thickness,
                                                epaper_2in15g_color_t color) {
    fill_landscape_display_rect(frame_buffer, x0, y0, width, thickness, color);
    fill_landscape_display_rect(frame_buffer, x0, y0 + height - thickness, width, thickness, color);
    fill_landscape_display_rect(frame_buffer, x0, y0, thickness, height, color);
    fill_landscape_display_rect(frame_buffer, x0 + width - thickness, y0, thickness, height, color);
}

static void draw_landscape_char(uint8_t *frame_buffer,
                                uint32_t x,
                                uint32_t y,
                                char c,
                                uint32_t scale,
                                epaper_2in15g_color_t foreground,
                                epaper_2in15g_color_t background) {
    const uint8_t *glyph = app_font5x7_get_glyph(c);

    for (uint32_t column = 0; column < APP_FONT5X7_WIDTH; column++) {
        for (uint32_t row = 0; row < APP_FONT5X7_HEIGHT; row++) {
            const bool pixel_on = (glyph[column] & (1u << row)) != 0;
            fill_landscape_display_rect(frame_buffer,
                                        x + column * scale,
                                        y + row * scale,
                                        scale,
                                        scale,
                                        pixel_on ? foreground : background);
        }
    }
}

static uint32_t landscape_text_advance(uint32_t scale) {
    return (APP_FONT5X7_WIDTH + 1) * scale;
}

static void draw_landscape_text(uint8_t *frame_buffer,
                                uint32_t x,
                                uint32_t y,
                                const char *text,
                                uint32_t scale,
                                epaper_2in15g_color_t foreground,
                                epaper_2in15g_color_t background) {
    const uint32_t advance = landscape_text_advance(scale);

    while (*text != '\0') {
        draw_landscape_char(frame_buffer, x, y, *text, scale, foreground, background);
        x += advance;
        text++;
    }
}

static void draw_landscape_text_wrapped(uint8_t *frame_buffer,
                                        uint32_t x,
                                        uint32_t y,
                                        uint32_t max_width,
                                        uint32_t max_lines,
                                        const char *text,
                                        uint32_t scale,
                                        epaper_2in15g_color_t foreground,
                                        epaper_2in15g_color_t background) {
    const uint32_t line_height = (APP_FONT5X7_HEIGHT + 2) * scale;
    const uint32_t start_x = x;
    const uint32_t max_x = start_x + max_width;
    uint32_t line = 0;

    while ((*text != '\0') && (line < max_lines)) {
        if ((*text == '\n') || ((x + APP_FONT5X7_WIDTH * scale) > max_x)) {
            line++;
            x = start_x;
            y += line_height;

            if (*text == '\n') {
                text++;
            }

            while (*text == ' ') {
                text++;
            }

            continue;
        }

        draw_landscape_char(frame_buffer, x, y, *text, scale, foreground, background);
        x += landscape_text_advance(scale);
        text++;
    }
}

void app_draw_test_pattern(uint8_t *frame_buffer) {
    app_draw_fill(frame_buffer, EPAPER_2IN15G_WHITE);

    const uint32_t band_height = EPAPER_2IN15G_HEIGHT / 4;
    fill_display_rect(frame_buffer, 0, 0, EPAPER_2IN15G_WIDTH, band_height, EPAPER_2IN15G_BLACK);
    fill_display_rect(frame_buffer,
                      0,
                      band_height,
                      EPAPER_2IN15G_WIDTH,
                      band_height,
                      EPAPER_2IN15G_WHITE);
    fill_display_rect(frame_buffer,
                      0,
                      band_height * 2,
                      EPAPER_2IN15G_WIDTH,
                      band_height,
                      EPAPER_2IN15G_YELLOW);
    fill_display_rect(frame_buffer,
                      0,
                      band_height * 3,
                      EPAPER_2IN15G_WIDTH,
                      EPAPER_2IN15G_HEIGHT - band_height * 3,
                      EPAPER_2IN15G_RED);
}

void app_draw_vocabulary_card(uint8_t *frame_buffer,
                              const vocabulary_card_t *card,
                              size_t card_index,
                              size_t card_count) {
    app_draw_fill(frame_buffer, EPAPER_2IN15G_WHITE);

    fill_landscape_display_rect(frame_buffer, 0, 0, 10, CARD_DISPLAY_HEIGHT, card->accent_color);
    fill_landscape_display_rect(frame_buffer,
                                10,
                                0,
                                CARD_DISPLAY_WIDTH - 10,
                                30,
                                EPAPER_2IN15G_BLACK);
    fill_landscape_display_rect(frame_buffer,
                                22,
                                46,
                                CARD_DISPLAY_WIDTH - 44,
                                22,
                                EPAPER_2IN15G_YELLOW);
    draw_landscape_display_rect_outline(frame_buffer, 22, 82, 126, 50, 2, EPAPER_2IN15G_BLACK);
    draw_landscape_display_rect_outline(frame_buffer, 166, 82, 108, 50, 2, EPAPER_2IN15G_RED);

    draw_landscape_text(frame_buffer,
                        22,
                        4,
                        card->word,
                        3,
                        EPAPER_2IN15G_WHITE,
                        EPAPER_2IN15G_BLACK);
    draw_landscape_text(frame_buffer,
                        24,
                        50,
                        card->phonetic,
                        2,
                        EPAPER_2IN15G_BLACK,
                        EPAPER_2IN15G_YELLOW);
    draw_landscape_text_wrapped(frame_buffer,
                                28,
                                90,
                                114,
                                3,
                                card->meaning,
                                1,
                                EPAPER_2IN15G_BLACK,
                                EPAPER_2IN15G_WHITE);
    draw_landscape_text_wrapped(frame_buffer,
                                172,
                                90,
                                96,
                                3,
                                card->note,
                                1,
                                EPAPER_2IN15G_RED,
                                EPAPER_2IN15G_WHITE);

    const uint32_t marker_width = 20;
    const uint32_t marker_gap = 10;
    const uint32_t marker_y = CARD_DISPLAY_HEIGHT - 18;

    for (size_t index = 0; index < card_count; index++) {
        const uint32_t marker_x = 24 + (uint32_t)index * (marker_width + marker_gap);
        const epaper_2in15g_color_t color =
            index == card_index ? card->accent_color : EPAPER_2IN15G_BLACK;
        fill_landscape_display_rect(frame_buffer, marker_x, marker_y, marker_width, 8, color);
    }
}
