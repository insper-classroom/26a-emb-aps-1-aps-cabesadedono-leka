#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "ili9341.h"
#include "gfx_ili9341.h"

#include "red.h"
#include "green.h"
#include "blue.h"
#include "yellow.h"
#include "error.h"
#include "victory.h"

#define DEBOUNCE_US 400000
#define SEQ_LENGTH 10
#define MAX_RANKING 5

// Flash: ultimo setor de 4KB para armazenar ranking
#define FLASH_RANKING_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define RANKING_MAGIC 0x52414E4B // "RANK"

#define COLOR_DISPLAY_MS 400
#define COLOR_GAP_MS 200
#define FEEDBACK_MS 200
#define TIMEOUT_MS 10000
#define LEVEL_DELAY_MS 800

// Pinos LCD (SPI via flat cable)
#define LCD_CLK   18
#define LCD_MOSI  19
#define LCD_CS    17
#define LCD_RST   16
#define LCD_LITE  20
#define LCD_DC    22

// Botoes e LEDs (3 cores, sem verde)
const int LED_PIN_WHITE = 15;
const int BTN_PIN_WHITE = 14;

const int LED_PIN_RED = 13;
const int BTN_PIN_RED = 12;

const int LED_PIN_BLUE = 11;
const int BTN_PIN_BLUE = 10;

const int AUDIO_PIN = 20;

// Display: 240x320, retrato
#define SCREEN_W 240
#define SCREEN_H 320

volatile int btn_pressed = -1;
volatile int alarm_flag = 0;
volatile int g_timer_0 = 0;

volatile uint64_t last_btn_red_time = 0;
volatile uint64_t last_btn_blue_time = 0;
volatile uint64_t last_btn_white_time = 0;

// Variaveis compartilhadas com IRQ do PWM
static const uint8_t *volatile audio_data = NULL;
static volatile uint32_t audio_length = 0;
static volatile uint32_t audio_position = 0;
static volatile int audio_playing = 0;

//---------------------------- CALLBACKS -------------------------------------

void pwm_interrupt_handler(void) {
    pwm_clear_irq(pwm_gpio_to_slice_num(AUDIO_PIN));
    if (audio_playing && audio_data != NULL) {
        if (audio_position < (audio_length << 3) - 1) {
            int sample = audio_data[audio_position >> 3];
            // Amplifica: expande desvio do centro (128) por 2x
            int amplified = 128 + (sample - 128) * 2;
            if (amplified > 255) amplified = 255;
            if (amplified < 0) amplified = 0;
            pwm_set_gpio_level(AUDIO_PIN, amplified);
            audio_position++;
        } else {
            audio_playing = 0;
            pwm_set_gpio_level(AUDIO_PIN, 0);
        }
    } else {
        pwm_set_gpio_level(AUDIO_PIN, 0);
    }
}

int64_t alarm_callback(alarm_id_t id, void *user_data) {
    alarm_flag = 1;
    return 0;
}

bool timer_0_callback(repeating_timer_t *rt) {
    g_timer_0 = 1;
    return true;
}

void btn_callback(uint gpio, uint32_t events) {
    if (events == 0x4) {
        uint64_t now = time_us_64();
        if (gpio == BTN_PIN_RED && (now - last_btn_red_time) > DEBOUNCE_US) {
            last_btn_red_time = now;
            btn_pressed = 1;
        } else if (gpio == BTN_PIN_BLUE && (now - last_btn_blue_time) > DEBOUNCE_US) {
            last_btn_blue_time = now;
            btn_pressed = 2;
        } else if (gpio == BTN_PIN_WHITE && (now - last_btn_white_time) > DEBOUNCE_US) {
            last_btn_white_time = now;
            btn_pressed = 3;
        }
    }
}

//---------------------------- HELPERS BASICOS --------------------------------

void all_leds_off(void) {
    gpio_put(LED_PIN_RED, 0);
    gpio_put(LED_PIN_BLUE, 0);
    gpio_put(LED_PIN_WHITE, 0);
}

void all_leds_on(void) {
    gpio_put(LED_PIN_RED, 1);
    gpio_put(LED_PIN_BLUE, 1);
    gpio_put(LED_PIN_WHITE, 1);
}

void led_on(int cor) {
    if (cor == 1) gpio_put(LED_PIN_RED, 1);
    else if (cor == 2) gpio_put(LED_PIN_BLUE, 1);
    else if (cor == 3) gpio_put(LED_PIN_WHITE, 1);
}

void play_audio(const uint8_t *data, uint32_t length) {
    audio_data = data;
    audio_length = length;
    audio_position = 0;
    audio_playing = 1;
}

void stop_audio(void) {
    audio_playing = 0;
    pwm_set_gpio_level(AUDIO_PIN, 0);
}

void play_color_audio(int cor) {
    if (cor == 1) play_audio(RED_DATA, RED_DATA_LENGTH);
    else if (cor == 2) play_audio(BLUE_DATA, BLUE_DATA_LENGTH);
    else if (cor == 3) play_audio(YELLOW_DATA, YELLOW_DATA_LENGTH);
}

void gerar_sequencia(int *sequence) {
    for (int i = 0; i < SEQ_LENGTH; i++) {
        sequence[i] = (rand() % 3) + 1;
    }
}

//---------------------------- HELPERS LCD ------------------------------------

uint16_t cor_to_color(int cor) {
    if (cor == 1) return ILI9341_RED;
    if (cor == 2) return ILI9341_BLUE;
    if (cor == 3) return ILI9341_WHITE;
    return ILI9341_BLACK;
}

const char *cor_to_name(int cor) {
    if (cor == 1) return "VERMELHO";
    if (cor == 2) return "AZUL";
    if (cor == 3) return "BRANCO";
    return "?";
}

void draw_text_centered(int y, const char *text, uint8_t size) {
    gfx_setTextSize(size);
    int w = gfx_getTextWidth(text);
    int x = (SCREEN_W - w) / 2;
    if (x < 0) x = 0;
    gfx_drawText(x, y, text);
}

//---------------------------- RANKING ----------------------------------------

void ranking_insert(int score, int *ranking, int *ranking_count) {
    if (score <= 0) return;

    int pos = *ranking_count;
    for (int i = 0; i < *ranking_count; i++) {
        if (score > ranking[i]) {
            pos = i;
            break;
        }
    }

    int limit = *ranking_count < MAX_RANKING ? *ranking_count : MAX_RANKING - 1;
    for (int i = limit; i > pos; i--) {
        ranking[i] = ranking[i - 1];
    }
    ranking[pos] = score;

    if (*ranking_count < MAX_RANKING) {
        (*ranking_count)++;
    }
}

void ranking_save(int *ranking, int ranking_count) {
    // Buffer alinhado a 256 bytes (tamanho minimo de escrita na flash)
    uint8_t buf[FLASH_PAGE_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    // Estrutura: [magic 4B][ranking_count 4B][ranking[0..4] 4B cada] = 28 bytes
    uint32_t *data = (uint32_t *)buf;
    data[0] = RANKING_MAGIC;
    data[1] = (uint32_t)ranking_count;
    for (int i = 0; i < MAX_RANKING; i++) {
        data[2 + i] = (uint32_t)ranking[i];
    }

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_RANKING_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_RANKING_OFFSET, buf, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

void ranking_load(int *ranking, int *ranking_count) {
    const uint8_t *flash_data = (const uint8_t *)(XIP_BASE + FLASH_RANKING_OFFSET);
    const uint32_t *data = (const uint32_t *)flash_data;

    if (data[0] != RANKING_MAGIC) {
        // Sem dados salvos — ranking vazio
        *ranking_count = 0;
        memset(ranking, 0, MAX_RANKING * sizeof(int));
        return;
    }

    *ranking_count = (int)data[1];
    if (*ranking_count > MAX_RANKING) *ranking_count = MAX_RANKING;
    for (int i = 0; i < MAX_RANKING; i++) {
        ranking[i] = (int)data[2 + i];
    }
}

//---------------------------- TELAS LCD --------------------------------------

void lcd_title_screen(int frame) {
    gfx_clear();

    // Titulo "GENIUS" em amarelo
    gfx_setTextColor(ILI9341_YELLOW);
    draw_text_centered(30, "GENIUS", 4);

    // Linha decorativa
    gfx_fillRect(20, 70, 200, 2, ILI9341_WHITE);

    // Quadradinhos das 3 cores
    int sq_size = 30;
    int gap = 20;
    int total_w = 3 * sq_size + 2 * gap;
    int start_x = (SCREEN_W - total_w) / 2;
    int sq_y = 90;

    gfx_fillRect(start_x, sq_y, sq_size, sq_size, ILI9341_RED);
    gfx_fillRect(start_x + sq_size + gap, sq_y, sq_size, sq_size, ILI9341_BLUE);
    gfx_fillRect(start_x + 2 * (sq_size + gap), sq_y, sq_size, sq_size, ILI9341_WHITE);

    // Texto piscante "Aperte qualquer botao"
    if (frame % 2 == 0) {
        gfx_setTextColor(ILI9341_WHITE);
        draw_text_centered(160, "Aperte o botao", 2);
        draw_text_centered(185, "BRANCO p/ iniciar", 2);
    }

    // Decoracao inferior
    gfx_setTextColor(ILI9341_CYAN);
    draw_text_centered(240, "~ * ~ * ~ * ~", 2);

    // Borda
    gfx_drawRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_WHITE, 2);
}

void lcd_show_level(int level) {
    gfx_clear();

    gfx_drawRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_WHITE, 2);

    gfx_setTextColor(ILI9341_YELLOW);
    draw_text_centered(40, "Observe!", 3);

    char buf[20];
    snprintf(buf, sizeof(buf), "Nivel: %d", level);
    gfx_setTextColor(ILI9341_WHITE);
    draw_text_centered(100, buf, 3);

    // Barra de progresso
    int bar_x = 20;
    int bar_y = 180;
    int bar_w = 200;
    int bar_h = 20;
    int fill_w = (level * bar_w) / SEQ_LENGTH;

    gfx_drawRect(bar_x, bar_y, bar_w, bar_h, ILI9341_WHITE, 1);
    if (fill_w > 0) {
        gfx_fillRect(bar_x, bar_y, fill_w, bar_h, ILI9341_GREEN);
    }
}

void lcd_show_color(int cor) {
    gfx_clear();

    uint16_t color = cor_to_color(cor);
    const char *nome = cor_to_name(cor);

    // Retangulo grande da cor
    gfx_fillRect(40, 40, 160, 160, color);

    // Nome da cor abaixo
    gfx_setTextColor(color);
    draw_text_centered(230, nome, 2);
}

void lcd_your_turn(int input_idx, int level) {
    gfx_clear();

    gfx_drawRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_CYAN, 2);

    gfx_setTextColor(ILI9341_CYAN);
    draw_text_centered(60, "Sua vez!", 3);

    char buf[20];
    snprintf(buf, sizeof(buf), "%d / %d", input_idx + 1, level);
    gfx_setTextColor(ILI9341_WHITE);
    draw_text_centered(130, buf, 4);
}

void lcd_correct(void) {
    gfx_clear();
    gfx_setTextColor(ILI9341_GREEN);
    draw_text_centered(120, "CERTO!", 4);
}

void lcd_game_over(int score) {
    gfx_clear();

    gfx_drawRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_RED, 3);

    gfx_setTextColor(ILI9341_RED);
    draw_text_centered(40, "GAME OVER", 3);

    gfx_fillRect(20, 80, 200, 2, ILI9341_RED);

    char buf[20];
    snprintf(buf, sizeof(buf), "Pontos: %d", score);
    gfx_setTextColor(ILI9341_WHITE);
    draw_text_centered(110, buf, 3);

    gfx_setTextColor(ILI9341_YELLOW);
    draw_text_centered(220, "Botao = ranking", 2);
}

void lcd_victory(void) {
    gfx_clear();

    gfx_drawRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_YELLOW, 3);

    gfx_setTextColor(ILI9341_YELLOW);
    draw_text_centered(40, "PARABENS!", 3);

    gfx_fillRect(20, 80, 200, 2, ILI9341_YELLOW);

    char buf[20];
    snprintf(buf, sizeof(buf), "Pontos: %d", SEQ_LENGTH);
    gfx_setTextColor(ILI9341_WHITE);
    draw_text_centered(110, buf, 3);

    gfx_setTextColor(ILI9341_GREEN);
    draw_text_centered(180, "PERFEITO!", 3);
}

void lcd_ranking_screen(const int *ranking, int ranking_count) {
    gfx_clear();

    gfx_drawRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_WHITE, 2);

    gfx_setTextColor(ILI9341_YELLOW);
    draw_text_centered(15, "RANKING", 3);

    gfx_fillRect(20, 50, 200, 2, ILI9341_WHITE);

    if (ranking_count == 0) {
        gfx_setTextColor(ILI9341_WHITE);
        draw_text_centered(120, "Sem pontuacao", 2);
    } else {
        for (int i = 0; i < ranking_count && i < 5; i++) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%d. %d pts", i + 1, ranking[i]);

            // Top 1 em dourado/amarelo
            if (i == 0) {
                gfx_setTextColor(ILI9341_YELLOW);
            } else {
                gfx_setTextColor(ILI9341_WHITE);
            }
            gfx_setTextSize(2);
            gfx_drawText(40, 70 + i * 35, buf);
        }
    }
}

void lcd_countdown(int n) {
    gfx_clear();

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", n);

    gfx_setTextColor(ILI9341_YELLOW);
    draw_text_centered(100, buf, 8);
}

//---------------------------- SETUP ------------------------------------------

void setup_audio(void) {
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    int audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    pwm_clear_irq(audio_pin_slice);
    pwm_set_irq_enabled(audio_pin_slice, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_interrupt_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 7.0f);
    pwm_config_set_wrap(&config, 255);
    pwm_init(audio_pin_slice, &config, true);
    pwm_set_gpio_level(AUDIO_PIN, 0);
}

void setup_pins(void) {
    // Botoes
    gpio_init(BTN_PIN_RED);
    gpio_set_dir(BTN_PIN_RED, GPIO_IN);
    gpio_pull_up(BTN_PIN_RED);

    gpio_init(BTN_PIN_BLUE);
    gpio_set_dir(BTN_PIN_BLUE, GPIO_IN);
    gpio_pull_up(BTN_PIN_BLUE);

    gpio_init(BTN_PIN_WHITE);
    gpio_set_dir(BTN_PIN_WHITE, GPIO_IN);
    gpio_pull_up(BTN_PIN_WHITE);

    // LEDs
    gpio_init(LED_PIN_RED);
    gpio_set_dir(LED_PIN_RED, GPIO_OUT);

    gpio_init(LED_PIN_BLUE);
    gpio_set_dir(LED_PIN_BLUE, GPIO_OUT);

    gpio_init(LED_PIN_WHITE);
    gpio_set_dir(LED_PIN_WHITE, GPIO_OUT);

    // Backlight do LCD
    gpio_init(LCD_LITE);
    gpio_set_dir(LCD_LITE, GPIO_OUT);
    gpio_put(LCD_LITE, 1);

    // IRQs
    gpio_set_irq_enabled_with_callback(BTN_PIN_RED, GPIO_IRQ_EDGE_FALL, true, &btn_callback);
    gpio_set_irq_enabled(BTN_PIN_BLUE, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BTN_PIN_WHITE, GPIO_IRQ_EDGE_FALL, true);
}

void setup_display(void) {
    LCD_setPins(LCD_DC, LCD_CS, LCD_RST, LCD_CLK, LCD_MOSI);
    LCD_setSPIperiph(spi0);
    LCD_initDisplay();
    LCD_setRotation(0);
    gfx_init();
}

//---------------------------- MAIN -------------------------------------------

int main() {
    stdio_init_all();
    sleep_ms(100);

    // Seed via ADC flutuante
    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);
    uint32_t seed = 0;
    for (int i = 0; i < 8; i++) {
        seed = (seed << 4) | (adc_read() & 0xF);
    }
    srand(seed);

    setup_pins();
    sleep_ms(50);
    setup_display();
    sleep_ms(50);
    setup_audio();

    int sequence[SEQ_LENGTH];
    int ranking[MAX_RANKING];
    int ranking_count = 0;

    gerar_sequencia(sequence);

    ranking_load(ranking, &ranking_count);

    repeating_timer_t timer_0;
    alarm_id_t alarm;
    int blink_count = 0;
    int blink_state = 0;

    int level = 0;
    int show_idx = 0;
    int input_idx = 0;
    int color_on = 0;
    int title_frame = 0;
    int final_score = 0;
    int countdown_val = 0;

    // 0=idle, 1=mostrando, 2=esperando input, 3=feedback acerto,
    // 4=erro, 5=vitoria, 6=game over screen, 7=ranking screen, 8=countdown
    int state = 0;

    lcd_title_screen(0);
    play_audio(VICTORY_DATA, VICTORY_DATA_LENGTH);

    while (true) {
//---------------------------- IDLE -------------------------------------
        if (state == 0) {
            title_frame++;
            if (title_frame % 50000 == 0) {
                lcd_title_screen(title_frame / 50000);
            }

            if (btn_pressed == 3) {
                btn_pressed = -1;
                alarm_flag = 0;

                gerar_sequencia(sequence);
                level = 1;
                show_idx = 0;
                color_on = 0;
                final_score = 0;

                countdown_val = 3;
                lcd_countdown(countdown_val);
                add_alarm_in_ms(600, alarm_callback, NULL, false);
                state = 8;
            } else {
                btn_pressed = -1;
            }
        }

//---------------------------- COUNTDOWN (sem sleep) -----------------------
        if (alarm_flag && state == 8) {
            alarm_flag = 0;
            countdown_val--;
            if (countdown_val > 0) {
                lcd_countdown(countdown_val);
                add_alarm_in_ms(600, alarm_callback, NULL, false);
            } else {
                lcd_show_level(level);
                state = 1;
                add_alarm_in_ms(LEVEL_DELAY_MS, alarm_callback, NULL, false);
            }
        }

//---------------------------- MOSTRANDO SEQUENCIA -------------------------
        if (alarm_flag && state == 1) {
            alarm_flag = 0;

            if (color_on) {
                all_leds_off();
                stop_audio();
                color_on = 0;
                show_idx++;

                if (show_idx < level) {
                    add_alarm_in_ms(COLOR_GAP_MS, alarm_callback, NULL, false);
                } else {
                    state = 2;
                    input_idx = 0;
                    btn_pressed = -1;
                    alarm_flag = 0;
                    lcd_your_turn(input_idx, level);
                    alarm = add_alarm_in_ms(TIMEOUT_MS, alarm_callback, NULL, false);
                }
            } else {
                if (show_idx < level) {
                    led_on(sequence[show_idx]);
                    play_color_audio(sequence[show_idx]);
                    lcd_show_color(sequence[show_idx]);
                    color_on = 1;
                    add_alarm_in_ms(COLOR_DISPLAY_MS, alarm_callback, NULL, false);
                }
            }
        }

//---------------------------- ESPERANDO INPUT -----------------------------
        if (state == 2) {
            if (alarm_flag) {
                alarm_flag = 0;
                final_score = level - 1;
                ranking_insert(final_score, ranking, &ranking_count);
                ranking_save(ranking, ranking_count);
                play_audio(ERROR_DATA, ERROR_DATA_LENGTH);
                lcd_game_over(final_score);
                state = 4;
                blink_count = 0;
                blink_state = 0;
                add_repeating_timer_ms(200, timer_0_callback, NULL, &timer_0);
            }
            if (btn_pressed != -1) {
                cancel_alarm(alarm);
                alarm_flag = 0;

                if (btn_pressed == sequence[input_idx]) {
                    state = 3;
                    led_on(btn_pressed);
                    play_color_audio(btn_pressed);
                    lcd_correct();
                    add_alarm_in_ms(FEEDBACK_MS, alarm_callback, NULL, false);
                } else {
                    btn_pressed = -1;
                    final_score = level - 1;
                    ranking_insert(final_score, ranking, &ranking_count);
                    ranking_save(ranking, ranking_count);
                    play_audio(ERROR_DATA, ERROR_DATA_LENGTH);
                    lcd_game_over(final_score);
                    state = 4;
                    blink_count = 0;
                    blink_state = 0;
                    add_repeating_timer_ms(200, timer_0_callback, NULL, &timer_0);
                    }
            }
        }

//---------------------------- FEEDBACK ACERTO -----------------------------
        if (alarm_flag && state == 3) {
            alarm_flag = 0;
            all_leds_off();
            stop_audio();
            input_idx++;
            btn_pressed = -1;

            if (input_idx >= level) {
                level++;
                if (level > SEQ_LENGTH) {
                    final_score = SEQ_LENGTH;
                    ranking_insert(final_score, ranking, &ranking_count);
                    ranking_save(ranking, ranking_count);
                    play_audio(VICTORY_DATA, VICTORY_DATA_LENGTH);
                    lcd_victory();
                    state = 5;
                    blink_count = 0;
                    blink_state = 0;
                    show_idx = 0;
                    add_repeating_timer_ms(150, timer_0_callback, NULL, &timer_0);
                    } else {
                    lcd_show_level(level);
                    state = 1;
                    show_idx = 0;
                    color_on = 0;
                    add_alarm_in_ms(LEVEL_DELAY_MS, alarm_callback, NULL, false);
                }
            } else {
                lcd_your_turn(input_idx, level);
                state = 2;
                alarm = add_alarm_in_ms(TIMEOUT_MS, alarm_callback, NULL, false);
            }
        }

//---------------------------- ANIMACAO ERRO -------------------------------
        if (g_timer_0 && state == 4) {
            g_timer_0 = 0;
            blink_state = !blink_state;
            if (blink_state) {
                all_leds_on();
                gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_RED);
            } else {
                all_leds_off();
                gfx_fillRect(0, 0, SCREEN_W, SCREEN_H, ILI9341_BLACK);
            }
            blink_count++;
            if (blink_count >= 6) {
                cancel_repeating_timer(&timer_0);
                g_timer_0 = 0;
                all_leds_off();
                stop_audio();
                lcd_game_over(final_score);
                state = 6;
            }
        }

//---------------------------- ANIMACAO VITORIA ----------------------------
        if (g_timer_0 && state == 5) {
            g_timer_0 = 0;
            all_leds_off();
            int leds[] = {LED_PIN_RED, LED_PIN_BLUE, LED_PIN_WHITE};
            gpio_put(leds[show_idx % 3], 1);
            show_idx++;
            blink_count++;
            if (blink_count >= 16) {
                cancel_repeating_timer(&timer_0);
                g_timer_0 = 0;
                all_leds_off();
                stop_audio();
                lcd_victory();
                state = 6;
            }
        }

//---------------------------- GAME OVER SCREEN ----------------------------
        if (state == 6) {
            if (btn_pressed != -1) {
                btn_pressed = -1;
                lcd_ranking_screen(ranking, ranking_count);
                state = 7;
            }
        }

//---------------------------- RANKING SCREEN ------------------------------
        if (state == 7) {
            if (btn_pressed != -1) {
                btn_pressed = -1;
                title_frame = 0;
                lcd_title_screen(0);
                state = 0;
            }
        }
    }

    return 0;
}
