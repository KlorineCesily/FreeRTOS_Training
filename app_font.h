#ifndef APP_FONT_H
#define APP_FONT_H

#include <stdint.h>

enum {
    APP_FONT5X7_WIDTH = 5,
    APP_FONT5X7_HEIGHT = 7,
};

const uint8_t *app_font5x7_get_glyph(char c);

#endif
