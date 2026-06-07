#ifndef APP_DRAW_H
#define APP_DRAW_H

#include <stddef.h>
#include <stdint.h>

#include "app_card.h"
#include "epaper_2in15g.h"

void app_draw_fill(uint8_t *frame_buffer, epaper_2in15g_color_t color);
void app_draw_test_pattern(uint8_t *frame_buffer);
void app_draw_vocabulary_card(uint8_t *frame_buffer,
                              const vocabulary_card_t *card,
                              size_t card_index,
                              size_t card_count);

#endif
