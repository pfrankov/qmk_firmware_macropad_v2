/* Copyright 2020 Geekboards ltd. (geekboards.ru / geekboards.de)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H
#include <math.h>
#include <string.h>

bool is_alt_tab_active = false;
uint16_t alt_tab_timer = 0;

// Упростим структуры, убрав неиспользуемые поля
typedef struct {
    uint8_t key_pos;
    uint16_t timer;
    bool active;
    uint8_t layer;
} reactive_state;

typedef struct {
    bool is_animating;
    uint16_t anim_timer;
    uint8_t prev_layer;
    uint8_t target_layer;
    int16_t block_offsets[2];  // Для верхнего и нижнего ряда
    uint16_t block_timers[2];  // Таймеры обновления блоков
} layer_animation_state;

// Инициализация с нулевыми значениями
static reactive_state reactive = {0};
static layer_animation_state layer_anim = {0};

// Константы для анимации
#define ANIM_TOTAL_TIME 1100
#define ANIM_FILL_TIME 1000
#define BLOCK_UPDATE_TOP 20
#define BLOCK_UPDATE_BOTTOM 30
#define EDGE_WIDTH 32
#define PROGRESS_START -64
#define PROGRESS_END 288

enum custom_keycodes {
    ALT_TAB = QK_KB_0
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_ortho_2x4(
        TG(1),   KC_F24,  KC_F23,  KC_F22,
        MC_1,    MC_0,    KC_F19,  KC_F18
    ),
    [1] = LAYOUT_ortho_2x4(
        TG(2),   S(KC_F18), S(KC_F19), S(KC_F20),
        KC_F16,  KC_F18,    KC_F19,    KC_F20
    ),
    [2] = LAYOUT_ortho_2x4(
        TO(0),   KC_F24,  KC_F23,  KC_F22,
        KC_F21,  KC_F20,  KC_F19,  KC_F18
    ),
    [3] = LAYOUT_ortho_2x4(
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    )
};

// Добавим вспомогательную функцию для безопасного сложения 8-битных чисел
static inline uint8_t safe_add_u8(uint8_t a, int16_t b) {
    int16_t sum = (int16_t)a + b;
    if (sum > 255) return 255;
    if (sum < 0) return 0;
    return (uint8_t)sum;
}

// Упростим функцию easing
static inline float easeInOutCubic(float x) {
    if (x < 0.5f) {
        return 4.0f * x * x * x;
    }
    float f = 2.0f * x - 2.0f;
    return 0.5f * f * f * f + 1.0f;
}

// Вспомогательная функция для получения HSV цвета слоя
static HSV get_layer_color(uint8_t layer) {
    switch (layer) {
        case 0: return (HSV){180, 255, 0};   // Черный
        case 1: return (HSV){213, 255, 26};  // Пурпурный
        case 2: return (HSV){128, 255, 26};  // Голубой
        default: return (HSV){0, 0, 26};     // Белый
    }
}


// Функция установки цвета слоя
static void set_layer_color(uint8_t layer) {
    rgb_matrix_enable();
    rgb_matrix_mode(RGB_MATRIX_SOLID_COLOR);
    HSV color = get_layer_color(layer);
    rgb_matrix_sethsv(color.h, color.s, color.v);
}

//------------ SUPER ALTTAB ---------------
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Обработка подсветки для всех нажатий
    if (record->event.pressed) {
        uint8_t row = record->event.key.row;
        uint8_t col = record->event.key.col;
        static const uint8_t led_map[2][4] = {{13,19,20,26}, {6,0,38,33}};
        reactive.key_pos = led_map[row][col];
        reactive.timer = timer_read();
        reactive.active = true;
        reactive.layer = get_highest_layer(layer_state);
    }

    // Обработка специальных клавиш
    switch (keycode) {
        case ALT_TAB:
            if (record->event.pressed) {
                if (!is_alt_tab_active) {
                    is_alt_tab_active = true;
                    register_code(KC_LALT);
                }
                alt_tab_timer = timer_read();
                register_code(KC_TAB);
            } else {
                unregister_code(KC_TAB);
            }
            break;

        // Обработка макросов
        case QK_MACRO_0 ... QK_MACRO_15: // Диапазон возможных макросов
            // Продолжаем обработку макроса, но эффект подсветки уже запущен
            break;
    }
    return true;
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (layer_anim.is_animating) {
        uint16_t elapsed = timer_elapsed(layer_anim.anim_timer);

        // Завершение анимации
        if (elapsed >= ANIM_TOTAL_TIME) {
            layer_anim.is_animating = false;
            set_layer_color(layer_anim.target_layer);
            return false;
        }

        // Расчет прогресса
        bool is_filling = elapsed < ANIM_FILL_TIME;
        float progress = is_filling ? (float)elapsed / ANIM_FILL_TIME : 1.0f;
        float eased = easeInOutCubic(progress);
        int16_t base_progress = PROGRESS_START + (int16_t)(eased * (PROGRESS_END - PROGRESS_START));

        // Обновление смещений блоков
        for (int row = 0; row < 2; row++) {
            if (elapsed > layer_anim.block_timers[row]) {
                uint16_t interval = row == 0 ? BLOCK_UPDATE_TOP : BLOCK_UPDATE_BOTTOM;
                layer_anim.block_timers[row] = elapsed + interval;
                layer_anim.block_offsets[row] = (rand() % 32) - 16;
            }
        }

        // Получаем цвета для анимации
        HSV start_hsv = get_layer_color(layer_anim.prev_layer);
        HSV end_hsv = get_layer_color(layer_anim.target_layer);

        // Для каждого светодиода создаем эффект загрузки блоками
        for (uint8_t i = led_min; i < led_max; i++) {
            uint8_t led_x = g_led_config.point[i].x;
            uint8_t led_y = g_led_config.point[i].y;

            // Определяем, к какому ряду относится светодиод
            uint8_t row = led_y > 32 ? 1 : 0;

            // Добавляем смещение блока с учетом расширенного диапазона
            int16_t current_progress = base_progress + layer_anim.block_offsets[row];

            // Создаем блоки размером 32 пикселя
            int16_t block_position = (led_x / 32) * 32;
            bool in_loading_zone = (block_position < current_progress);

            // Изменяем логику глитчей
            bool block_glitch;
            if (is_filling) {
                // Во время заполнения - меньше глитчей
                block_glitch = (rand() % 100) < 8;
            } else {
                // После заполнения - более редкие глитчи
                block_glitch = (rand() % 100) < 8; // Уменьшили с 25 до 8
            }

            // Откаты только во время заполнения
            bool block_rollback = is_filling && (rand() % 100) < 3;

            HSV current_hsv;
            if (block_rollback) {
                current_hsv = start_hsv;
            } else if (in_loading_zone) {
                current_hsv = end_hsv;

                if (block_glitch) {
                    // Более интенсивные глитчи после заполнения
                    int16_t hue_shift;
                    if (is_filling) {
                        hue_shift = (rand() % 41) - 20;
                    } else {
                        hue_shift = (rand() % 61) - 30; // Уменьшили диапазон с 81 до 61
                    }
                    current_hsv.h = (current_hsv.h + hue_shift) % 256;

                    // Более редкие и менее яркие вспышки после заполнения
                    if (!is_filling && (rand() % 12 == 0)) { // Уменьшили частоту с 4 до 12
                        current_hsv.v = safe_add_u8(current_hsv.v, 128); // Уменьшили яркость вспышек
                    } else if (rand() % 2) {
                        current_hsv.v = safe_add_u8(current_hsv.v, (rand() % 51) - 25); // Уменьшили диапазон с 71 до 51
                    }
                }
            } else {
                current_hsv = start_hsv;
            }

            // Эффект краёв блоков
            int16_t edge_distance = abs(block_position - current_progress);
            if (edge_distance < 32) {
                float edge_intensity = (32 - edge_distance) / 32.0f;
                edge_intensity = easeInOutCubic(edge_intensity);
                current_hsv.v = safe_add_u8(current_hsv.v, (uint8_t)(edge_intensity * 96));
            }

            RGB rgb = hsv_to_rgb(current_hsv);
            rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
        }

        return false;
    }

    // Реактивная подсветка обрабатывается только если нет анимации смены слоя
    if (reactive.active) {
        uint8_t key_x = g_led_config.point[reactive.key_pos].x;
        uint8_t key_y = g_led_config.point[reactive.key_pos].y;

        // Определяем цвет вспышки (немного отличающийся от цвета слоя)
        HSV hsv;
        uint8_t max_brightness;
        switch (reactive.layer) {
            case 0:
                hsv = (HSV){180, 255, 255}; // Насыщенный чистый синий
                max_brightness = 128;        // 50% яркости для нулевого слоя
                break;
            case 1:
                hsv = (HSV){225, 255, 255}; // Более холодный пурпурный
                max_brightness = 255;        // Полная яркость
                break;
            case 2:
                hsv = (HSV){140, 255, 255}; // Более яркий голубой
                max_brightness = 255;        // Полная яркость
                break;
            default:
                return false;
        }

        // Затухание эффекта со временем
        uint16_t time_elapsed = timer_elapsed(reactive.timer);
        if (time_elapsed > 500) {
            reactive.active = false;
            return false;
        }

        // Получаем базовую яркость текущего слоя
        uint8_t base_val = (reactive.layer == 0) ? 0 : 26; // ~10% от 255

        // Создаем эффект вспышки с возвратом к базовой яркости
        uint8_t flash_intensity = base_val + (((max_brightness - base_val) * (500 - time_elapsed)) / 500);
        hsv.v = flash_intensity;

        // Применяем эффект только к близлежащим светодиодам
        for (uint8_t i = led_min; i < led_max; i++) {
            int32_t dx = (int32_t)g_led_config.point[i].x - key_x;
            int32_t dy = (int32_t)g_led_config.point[i].y - key_y;
            uint32_t distance_squared = dx * dx + dy * dy;

            const uint32_t max_distance = 1000;

            if (distance_squared < max_distance) {
                HSV local_hsv = hsv;

                // Рассчитываем интенсивность с учетом расстояния
                uint8_t distance_fade = (uint8_t)(255 * (max_distance - distance_squared) / max_distance);

                // Комбинируем базовую яркость и эффект вспышки
                uint8_t final_val = base_val + (((flash_intensity - base_val) * distance_fade) / 255);
                local_hsv.v = final_val;

                RGB rgb = hsv_to_rgb(local_hsv);
                rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
            }
        }

        return false;
    }

    return true;
}

void matrix_scan_user(void) {
    if (is_alt_tab_active) {
        if (timer_elapsed(alt_tab_timer) > 1000) {
            unregister_code(KC_LALT);
            is_alt_tab_active = false;
        }
    }
}

void keyboard_post_init_user(void) {
    // Устанавливаем черный цвет вместо выключения
    rgb_matrix_enable();
    rgb_matrix_mode(RGB_MATRIX_SOLID_COLOR);
    rgb_matrix_sethsv(0, 0, 0);
}

layer_state_t layer_state_set_user(layer_state_t state) {
    uint8_t target_layer = get_highest_layer(state);

    // Запускаем анимацию при любой смене слоя
    if (target_layer != layer_anim.target_layer) {
        // Немедленно отключаем реактивную подсветку
        reactive.active = false;

        // Сохраняем текущий слой как предыдущий, если анимация не идёт
        if (!layer_anim.is_animating) {
            layer_anim.prev_layer = layer_anim.target_layer;
        }
        // Обновляем целевой слой
        layer_anim.target_layer = target_layer;

        // Перезапускаем анимацию
        layer_anim.is_animating = true;
        layer_anim.anim_timer = timer_read();
    }

    return state;
}
