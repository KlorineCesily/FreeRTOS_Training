#include "epaper_2in15g.h"

#include <stddef.h>

#include "hardware/gpio.h"
#include "pico/stdlib.h"

enum {
    EPAPER_RESET_HIGH_MS = 200,
    EPAPER_RESET_LOW_MS = 10,
    EPAPER_POWER_STABLE_MS = 100,
    EPAPER_BUSY_POLL_MS = 5,
    EPAPER_BUSY_TIMEOUT_MS = 30000,
};

static void epaper_gpio_output(uint pin, bool value) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, value);
}

static void epaper_write_command(uint8_t command) {
    gpio_put(EPAPER_2IN15G_PIN_DC, 0);
    gpio_put(EPAPER_2IN15G_PIN_CS, 0);
    spi_write_blocking(EPAPER_2IN15G_SPI_PORT, &command, 1);
    gpio_put(EPAPER_2IN15G_PIN_CS, 1);
}

static void epaper_write_data(uint8_t data) {
    gpio_put(EPAPER_2IN15G_PIN_DC, 1);
    gpio_put(EPAPER_2IN15G_PIN_CS, 0);
    spi_write_blocking(EPAPER_2IN15G_SPI_PORT, &data, 1);
    gpio_put(EPAPER_2IN15G_PIN_CS, 1);
}

static void epaper_write_data_block(const uint8_t *data, size_t length) {
    gpio_put(EPAPER_2IN15G_PIN_DC, 1);
    gpio_put(EPAPER_2IN15G_PIN_CS, 0);
    spi_write_blocking(EPAPER_2IN15G_SPI_PORT, data, length);
    gpio_put(EPAPER_2IN15G_PIN_CS, 1);
}

static void epaper_write_command_data(uint8_t command, const uint8_t *data, size_t length) {
    epaper_write_command(command);

    for (size_t index = 0; index < length; index++) {
        epaper_write_data(data[index]);
    }
}

static void epaper_reset(void) {
    gpio_put(EPAPER_2IN15G_PIN_RST, 1);
    sleep_ms(EPAPER_RESET_HIGH_MS);
    gpio_put(EPAPER_2IN15G_PIN_RST, 0);
    sleep_ms(EPAPER_RESET_LOW_MS);
    gpio_put(EPAPER_2IN15G_PIN_RST, 1);
    sleep_ms(EPAPER_RESET_HIGH_MS);
}

static bool epaper_wait_until_busy_high(uint32_t timeout_ms) {
    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (!gpio_get(EPAPER_2IN15G_PIN_BUSY)) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            return false;
        }

        sleep_ms(EPAPER_BUSY_POLL_MS);
    }

    return true;
}

static bool epaper_turn_on_display(void) {
    epaper_write_command(0x12);
    epaper_write_data(0x00);
    return epaper_wait_until_busy_high(EPAPER_BUSY_TIMEOUT_MS);
}

void epaper_2in15g_init_io(void) {
    spi_init(EPAPER_2IN15G_SPI_PORT, EPAPER_2IN15G_BAUDRATE_HZ);
    spi_set_format(EPAPER_2IN15G_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(EPAPER_2IN15G_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(EPAPER_2IN15G_PIN_SCK, GPIO_FUNC_SPI);

    epaper_gpio_output(EPAPER_2IN15G_PIN_CS, 1);
    epaper_gpio_output(EPAPER_2IN15G_PIN_DC, 0);
    epaper_gpio_output(EPAPER_2IN15G_PIN_RST, 1);
    epaper_gpio_output(EPAPER_2IN15G_PIN_PWR, 1);
    sleep_ms(EPAPER_POWER_STABLE_MS);

    gpio_init(EPAPER_2IN15G_PIN_BUSY);
    gpio_set_dir(EPAPER_2IN15G_PIN_BUSY, GPIO_IN);
    gpio_pull_up(EPAPER_2IN15G_PIN_BUSY);
}

bool epaper_2in15g_init_panel(void) {
    epaper_reset();

    static const uint8_t command_4d[] = { 0x78 };
    static const uint8_t command_00[] = { 0x0f, 0x29 };
    static const uint8_t command_01[] = { 0x07, 0x00 };
    static const uint8_t command_03[] = { 0x10, 0x54, 0x44 };
    static const uint8_t command_06[] = { 0x0f, 0x0a, 0x2f, 0x25, 0x22, 0x2e, 0x21 };
    static const uint8_t command_30[] = { 0x02 };
    static const uint8_t command_41[] = { 0x00 };
    static const uint8_t command_50[] = { 0x37 };
    static const uint8_t command_60[] = { 0x02, 0x02 };
    static const uint8_t command_61[] = {
        EPAPER_2IN15G_WIDTH / 256,
        EPAPER_2IN15G_WIDTH % 256,
        EPAPER_2IN15G_HEIGHT / 256,
        EPAPER_2IN15G_HEIGHT % 256,
    };
    static const uint8_t command_65[] = { 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t command_e7[] = { 0x1c };
    static const uint8_t command_e3[] = { 0x22 };
    static const uint8_t command_e0[] = { 0x00 };
    static const uint8_t command_b4[] = { 0xd0 };
    static const uint8_t command_b5[] = { 0x03 };
    static const uint8_t command_e9[] = { 0x01 };

    epaper_write_command_data(0x4d, command_4d, sizeof(command_4d));
    epaper_write_command_data(0x00, command_00, sizeof(command_00));
    epaper_write_command_data(0x01, command_01, sizeof(command_01));
    epaper_write_command_data(0x03, command_03, sizeof(command_03));
    epaper_write_command_data(0x06, command_06, sizeof(command_06));
    epaper_write_command_data(0x30, command_30, sizeof(command_30));
    epaper_write_command_data(0x41, command_41, sizeof(command_41));
    epaper_write_command_data(0x50, command_50, sizeof(command_50));
    epaper_write_command_data(0x60, command_60, sizeof(command_60));
    epaper_write_command_data(0x61, command_61, sizeof(command_61));
    epaper_write_command_data(0x65, command_65, sizeof(command_65));
    epaper_write_command_data(0xe7, command_e7, sizeof(command_e7));
    epaper_write_command_data(0xe3, command_e3, sizeof(command_e3));
    epaper_write_command_data(0xe0, command_e0, sizeof(command_e0));
    epaper_write_command_data(0xb4, command_b4, sizeof(command_b4));
    epaper_write_command_data(0xb5, command_b5, sizeof(command_b5));
    epaper_write_command_data(0xe9, command_e9, sizeof(command_e9));

    epaper_write_command(0x04);
    return epaper_wait_until_busy_high(EPAPER_BUSY_TIMEOUT_MS);
}

bool epaper_2in15g_clear(epaper_2in15g_color_t color) {
    const uint8_t fill = epaper_2in15g_pack4(color, color, color, color);

    epaper_write_command(0x10);

    for (uint32_t index = 0; index < EPAPER_2IN15G_BUFFER_SIZE; index++) {
        epaper_write_data(fill);
    }

    return epaper_turn_on_display();
}

bool epaper_2in15g_display(const uint8_t *image) {
    epaper_write_command(0x10);
    epaper_write_data_block(image, EPAPER_2IN15G_BUFFER_SIZE);
    return epaper_turn_on_display();
}

void epaper_2in15g_sleep(void) {
    epaper_write_command(0x02);
    epaper_write_data(0x00);
    epaper_write_command(0x07);
    epaper_write_data(0xa5);
    gpio_put(EPAPER_2IN15G_PIN_PWR, 0);
}

bool epaper_2in15g_busy_level(void) {
    return gpio_get(EPAPER_2IN15G_PIN_BUSY);
}

uint8_t epaper_2in15g_pack4(epaper_2in15g_color_t p0,
                            epaper_2in15g_color_t p1,
                            epaper_2in15g_color_t p2,
                            epaper_2in15g_color_t p3) {
    return (uint8_t)((p0 << 6) | (p1 << 4) | (p2 << 2) | p3);
}
