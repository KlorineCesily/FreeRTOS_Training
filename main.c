#include <stdio.h>

#include "pico/stdlib.h"

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    while (true) {
        printf("Hello, direct run!\r\n");
        sleep_ms(1000);
    }
}
