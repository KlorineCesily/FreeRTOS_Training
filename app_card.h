#ifndef APP_CARD_H
#define APP_CARD_H

#include <stddef.h>

#include "epaper_2in15g.h"

typedef struct {
    const char *word;
    const char *phonetic;
    const char *meaning;
    const char *example;
    const char *note;
    epaper_2in15g_color_t accent_color;
} vocabulary_card_t;

size_t app_card_count(void);
const vocabulary_card_t *app_card_get(size_t index);
size_t app_card_next(size_t index);
size_t app_card_prev(size_t index);

#endif
