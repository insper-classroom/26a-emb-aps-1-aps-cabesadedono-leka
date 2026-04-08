#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define BTN_BRANCO  15
#define BTN_AMARELO 14
#define BTN_VERDE   13

#define LED_BRANCO  16
#define LED_AMARELO 17
#define LED_VERDE   18

int main() {
    stdio_init_all();

    // Botões com pull-up
    gpio_init(BTN_BRANCO);  gpio_set_dir(BTN_BRANCO, GPIO_IN);  gpio_pull_up(BTN_BRANCO);
    gpio_init(BTN_AMARELO); gpio_set_dir(BTN_AMARELO, GPIO_IN); gpio_pull_up(BTN_AMARELO);
    gpio_init(BTN_VERDE);   gpio_set_dir(BTN_VERDE, GPIO_IN);   gpio_pull_up(BTN_VERDE);

    // LEDs
    gpio_init(LED_BRANCO);  gpio_set_dir(LED_BRANCO, GPIO_OUT);
    gpio_init(LED_AMARELO); gpio_set_dir(LED_AMARELO, GPIO_OUT);
    gpio_init(LED_VERDE);   gpio_set_dir(LED_VERDE, GPIO_OUT);

    while (true) {
        if (!gpio_get(BTN_BRANCO)) {
            gpio_put(LED_BRANCO, 1);
            sleep_ms(500);
            gpio_put(LED_BRANCO, 0);
        }
        if (!gpio_get(BTN_AMARELO)) {
            gpio_put(LED_AMARELO, 1);
            sleep_ms(500);
            gpio_put(LED_AMARELO, 0);
        }
        if (!gpio_get(BTN_VERDE)) {
            gpio_put(LED_VERDE, 1);
            sleep_ms(500);
            gpio_put(LED_VERDE, 0);
        }
        sleep_ms(10);
    }
}