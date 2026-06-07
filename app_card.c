#include "app_card.h"

static const vocabulary_card_t vocabulary_cards[] = {
    {
        .word = "aberration",
        .phonetic = "/ae-buh-RAY-shun/",
        .meaning = "a departure from what is normal",
        .example = "The sudden result was an aberration, not a trend.",
        .note = "ab- away + errare wander",
        .accent_color = EPAPER_2IN15G_RED,
    },
    {
        .word = "mitigate",
        .phonetic = "/MIT-uh-gayt/",
        .meaning = "to make something less severe",
        .example = "Good planning can mitigate the risk.",
        .note = "exam sense: reduce harm or severity",
        .accent_color = EPAPER_2IN15G_YELLOW,
    },
    {
        .word = "resilient",
        .phonetic = "/ri-ZIL-yuhnt/",
        .meaning = "able to recover quickly",
        .example = "A resilient system keeps working after failure.",
        .note = "re- back + salire leap",
        .accent_color = EPAPER_2IN15G_BLACK,
    },
};

size_t app_card_count(void) {
    return sizeof(vocabulary_cards) / sizeof(vocabulary_cards[0]);
}

const vocabulary_card_t *app_card_get(size_t index) {
    return &vocabulary_cards[index % app_card_count()];
}

size_t app_card_next(size_t index) {
    return (index + 1) % app_card_count();
}

size_t app_card_prev(size_t index) {
    return (index + app_card_count() - 1) % app_card_count();
}
