#include "pico/stdlib.h"

int main() {
    // Testa todos os 4 LEDs piscando em sequencia
    const int leds[] = {13,11 , 15, 9}; // RED, BLUE, WHITE, GREEN

    for (int i = 0; i < 4; i++) {
        gpio_init(leds[i]);
        gpio_set_dir(leds[i], GPIO_OUT);
    }

    while (1) {
        for (int i = 0; i < 4; i++) {
            gpio_put(leds[i], 1);
            sleep_ms(250);
            gpio_put(leds[i], 0);
            sleep_ms(250);
        }
    }
}
