#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "ili9341.h"
#include "gfx_ili9341.h"

// Pinos LCD
#define LCD_CLK   18
#define LCD_MOSI  19
#define LCD_CS    17
#define LCD_RST   16
#define LCD_LITE  21
#define LCD_DC    22

// Botoes e LEDs
#define BTN_RED   2
#define LED_RED   3
#define BTN_BLUE  4
#define LED_BLUE  5
#define LED_WHITE 6
#define BTN_WHITE 7
#define LED_GREEN 14
#define BTN_GREEN 15

#define AUDIO_PIN 20

// ===================== TESTE 1: LEDs =====================
void test_leds(void) {
    int leds[] = {LED_RED, LED_GREEN, LED_BLUE, LED_WHITE};
    const char *names[] = {"RED(GP3)", "GREEN(GP14)", "BLUE(GP5)", "WHITE(GP6)"};

    printf("\n=== TESTE 1: LEDs ===\n");

    for (int i = 0; i < 4; i++) {
        gpio_init(leds[i]);
        gpio_set_dir(leds[i], GPIO_OUT);
    }

    for (int i = 0; i < 4; i++) {
        printf("  LED %s -> ON\n", names[i]);
        gpio_put(leds[i], 1);
        sleep_ms(500);
        gpio_put(leds[i], 0);
        sleep_ms(200);
    }

    printf("  Todos ON...\n");
    for (int i = 0; i < 4; i++) gpio_put(leds[i], 1);
    sleep_ms(1000);
    for (int i = 0; i < 4; i++) gpio_put(leds[i], 0);

    printf("  LEDs OK!\n");
}

// ===================== TESTE 2: BOTOES =====================
void test_buttons(void) {
    int btns[] = {BTN_RED, BTN_GREEN, BTN_BLUE, BTN_WHITE};
    int leds[] = {LED_RED, LED_GREEN, LED_BLUE, LED_WHITE};
    const char *names[] = {"RED(GP2)", "GREEN(GP15)", "BLUE(GP4)", "WHITE(GP7)"};

    printf("\n=== TESTE 2: BOTOES ===\n");
    printf("  Aperte cada botao. Timeout 10s por botao.\n");
    printf("  O LED correspondente acende ao pressionar.\n\n");

    for (int i = 0; i < 4; i++) {
        gpio_init(btns[i]);
        gpio_set_dir(btns[i], GPIO_IN);
        gpio_pull_up(btns[i]);
    }

    for (int i = 0; i < 4; i++) {
        printf("  Aguardando botao %s...", names[i]);
        uint32_t start = to_ms_since_boot(get_absolute_time());
        int pressed = 0;

        while (to_ms_since_boot(get_absolute_time()) - start < 10000) {
            if (!gpio_get(btns[i])) {
                pressed = 1;
                gpio_put(leds[i], 1);
                printf(" OK!\n");
                sleep_ms(300);
                gpio_put(leds[i], 0);
                break;
            }
            sleep_ms(10);
        }

        if (!pressed) {
            printf(" TIMEOUT - nao pressionado\n");
        }
    }
}

// ===================== TESTE 3: ADC =====================
void test_adc(void) {
    printf("\n=== TESTE 3: ADC (GP26) ===\n");

    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);

    printf("  Lendo 10 amostras do pino flutuante GP26:\n  ");
    for (int i = 0; i < 10; i++) {
        uint16_t val = adc_read();
        printf("%d ", val);
        sleep_ms(50);
    }
    printf("\n  (valores devem variar se o pino esta flutuante)\n");
}

// ===================== TESTE 4: AUDIO PWM =====================
void test_audio(void) {
    printf("\n=== TESTE 4: AUDIO PWM (GP20) ===\n");

    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    int slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 8.0f);
    pwm_config_set_wrap(&config, 250);
    pwm_init(slice, &config, true);

    printf("  Tocando tom 440Hz por 1s...\n");

    // Gerar tom simples via polling
    uint32_t sys_clk = clock_get_hz(clk_sys);
    printf("  sys_clock = %lu Hz\n", sys_clk);

    for (int t = 0; t < 11000; t++) {
        // Senoide simplificada: onda quadrada 440Hz
        int period_samples = 11000 / 440; // ~25
        int half = period_samples / 2;
        int pos = t % period_samples;
        uint8_t val = (pos < half) ? 200 : 50;
        pwm_set_gpio_level(AUDIO_PIN, val);
        sleep_us(91); // ~11kHz
    }

    pwm_set_gpio_level(AUDIO_PIN, 0);
    printf("  Audio OK! (ouviu um bip?)\n");
}

// ===================== TESTE 5: LCD BACKLIGHT =====================
void test_backlight(void) {
    printf("\n=== TESTE 5: LCD BACKLIGHT (GP21) ===\n");

    gpio_init(LCD_LITE);
    gpio_set_dir(LCD_LITE, GPIO_OUT);

    printf("  Backlight OFF...\n");
    gpio_put(LCD_LITE, 0);
    sleep_ms(1000);

    printf("  Backlight ON...\n");
    gpio_put(LCD_LITE, 1);
    sleep_ms(1000);

    printf("  Backlight OK!\n");
}

// ===================== TESTE 6: LCD SPI =====================
void test_lcd(void) {
    printf("\n=== TESTE 6: LCD ILI9341 (SPI) ===\n");

    printf("  Configurando pinos...\n");
    LCD_setPins(LCD_DC, LCD_CS, LCD_RST, LCD_CLK, LCD_MOSI);
    LCD_setSPIperiph(spi0);

    printf("  Inicializando display...\n");
    LCD_initDisplay();
    LCD_setRotation(0);
    gfx_init();

    printf("  Tela VERMELHA...\n");
    gfx_fillRect(0, 0, 240, 320, ILI9341_RED);
    sleep_ms(1000);

    printf("  Tela VERDE...\n");
    gfx_fillRect(0, 0, 240, 320, ILI9341_GREEN);
    sleep_ms(1000);

    printf("  Tela AZUL...\n");
    gfx_fillRect(0, 0, 240, 320, ILI9341_BLUE);
    sleep_ms(1000);

    printf("  Tela PRETA + texto...\n");
    gfx_clear();
    gfx_setTextColor(ILI9341_WHITE);
    gfx_setTextSize(2);
    gfx_drawText(10, 10, "LCD OK!");
    gfx_setTextColor(ILI9341_YELLOW);
    gfx_setTextSize(3);
    gfx_drawText(10, 40, "GENIUS");

    gfx_fillRect(10, 80, 50, 50, ILI9341_RED);
    gfx_fillRect(70, 80, 50, 50, ILI9341_GREEN);
    gfx_fillRect(130, 80, 50, 50, ILI9341_BLUE);
    gfx_fillRect(190, 80, 50, 50, ILI9341_WHITE);

    gfx_setTextColor(ILI9341_WHITE);
    gfx_setTextSize(1);
    gfx_drawText(10, 150, "Se voce ve isso, LCD funciona!");

    printf("  LCD OK!\n");
}

// ===================== TESTE INTERATIVO =====================
void test_interactive(void) {
    int btns[] = {BTN_RED, BTN_GREEN, BTN_BLUE, BTN_WHITE};
    int leds[] = {LED_RED, LED_GREEN, LED_BLUE, LED_WHITE};
    uint16_t colors[] = {ILI9341_RED, ILI9341_GREEN, ILI9341_BLUE, ILI9341_WHITE};
    const char *names[] = {"VERMELHO", "VERDE", "AZUL", "BRANCO"};

    printf("\n=== TESTE 7: INTERATIVO ===\n");
    printf("  Aperte botoes -> LED acende + cor na tela.\n");
    printf("  Aguardando 30s...\n\n");

    gfx_clear();
    gfx_setTextColor(ILI9341_YELLOW);
    gfx_setTextSize(2);
    gfx_drawText(10, 10, "TESTE BOTOES");
    gfx_setTextColor(ILI9341_WHITE);
    gfx_setTextSize(1);
    gfx_drawText(10, 40, "Aperte os botoes!");

    uint32_t start = to_ms_since_boot(get_absolute_time());

    while (to_ms_since_boot(get_absolute_time()) - start < 30000) {
        for (int i = 0; i < 4; i++) {
            if (!gpio_get(btns[i])) {
                gpio_put(leds[i], 1);
                gfx_fillRect(20, 60, 200, 200, colors[i]);
                gfx_setTextColor(i == 3 ? ILI9341_BLACK : ILI9341_WHITE);
                gfx_setTextSize(2);
                int w = 6 * 2 * strlen(names[i]);
                gfx_drawText((240 - w) / 2, 140, names[i]);
                printf("  Botao %s pressionado\n", names[i]);

                while (!gpio_get(btns[i])) sleep_ms(10);
                gpio_put(leds[i], 0);
                sleep_ms(100);
            }
        }
        sleep_ms(10);
    }

    printf("  Teste interativo finalizado.\n");
}

// ===================== MAIN =====================
int main() {
    stdio_init_all();

    // Aguarda USB serial conectar (5s max)
    for (int i = 0; i < 50 && !stdio_usb_connected(); i++) {
        sleep_ms(100);
    }
    sleep_ms(500);

    printf("\n");
    printf("================================\n");
    printf("  TESTE HARDWARE - GENIUS PICO\n");
    printf("================================\n");
    printf("  Board: Pico 2 (RP2350)\n");

    uint32_t sys_clk = clock_get_hz(clk_sys);
    printf("  SysClk: %lu Hz\n", sys_clk);

    test_leds();
    test_buttons();
    test_adc();
    test_audio();
    test_backlight();
    test_lcd();
    test_interactive();

    printf("\n================================\n");
    printf("  TODOS OS TESTES CONCLUIDOS\n");
    printf("================================\n");
    printf("  Reinicie para repetir.\n");

    while (1) sleep_ms(1000);

    return 0;
}
