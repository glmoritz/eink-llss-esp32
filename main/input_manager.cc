#include "input_manager.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>

static const char* TAG = "InputManager";

InputManager::InputManager(i2c_master_bus_handle_t i2c_bus, uint8_t mcp_addr, gpio_num_t mcp_int_pin,
                           gpio_num_t enter_pin, gpio_num_t esc_pin,
                           gpio_num_t hl_left_pin, gpio_num_t hl_right_pin)
    : mcp_(i2c_bus, mcp_addr, mcp_int_pin),
      enter_pin_(enter_pin), esc_pin_(esc_pin),
      hl_left_pin_(hl_left_pin), hl_right_pin_(hl_right_pin),
      callback_(nullptr) {
    memset(gpio_buttons_, 0, sizeof(gpio_buttons_));
}

InputManager::~InputManager() {
}

void InputManager::InitGpioButton(gpio_num_t pin) {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
}

void InputManager::Init() {
    ESP_LOGI(TAG, "Initializing input manager");

    // Initialize MCP23017 for matrix buttons BTN_1..BTN_8
    esp_err_t mcp_ret = mcp_.Init();
    if (mcp_ret == ESP_OK) {
        mcp_.SetCallback([this](uint8_t button_index, bool pressed) {
            if (callback_ && button_index < 8) {
                ButtonEvent ev;
                ev.button = static_cast<ButtonId>(button_index);
                ev.type = pressed ? ButtonEventType::PRESS : ButtonEventType::RELEASE;
                ev.timestamp_ms = esp_timer_get_time() / 1000;
                callback_(ev);
            }
        });
        ESP_LOGI(TAG, "MCP23017 buttons (BTN_1..BTN_8) available");
    } else {
        ESP_LOGW(TAG, "MCP23017 not available, buttons BTN_1..BTN_8 will not work");
    }

    // Initialize direct GPIO buttons
    gpio_buttons_[0] = {enter_pin_, ButtonId::ENTER, true, 0, false, 0, false};
    gpio_buttons_[1] = {esc_pin_, ButtonId::ESC, true, 0, false, 0, false};
    gpio_buttons_[2] = {hl_left_pin_, ButtonId::HL_LEFT, true, 0, false, 0, false};
    gpio_buttons_[3] = {hl_right_pin_, ButtonId::HL_RIGHT, true, 0, false, 0, false};

    for (auto& btn : gpio_buttons_) {
        InitGpioButton(btn.pin);
    }

    ESP_LOGI(TAG, "Input manager initialized");
}

void InputManager::SetEventCallback(EventCallback callback) {
    callback_ = callback;
}

void InputManager::PollGpioButton(GpioState& state) {
    bool level = gpio_get_level(state.pin) != 0;
    int64_t now = esp_timer_get_time() / 1000;

    // Debounce
    if (level != state.last_level) {
        state.last_change_ms = now;
        state.last_level = level;
        return;
    }

    if ((now - state.last_change_ms) < DEBOUNCE_MS) {
        return;
    }

    bool currently_pressed = !level;  // Active low

    if (currently_pressed && !state.pressed) {
        // Press detected
        state.pressed = true;
        state.press_start_ms = now;
        state.long_press_sent = false;
        if (callback_) {
            ButtonEvent ev;
            ev.button = state.button;
            ev.type = ButtonEventType::PRESS;
            ev.timestamp_ms = now;
            callback_(ev);
        }
    } else if (currently_pressed && state.pressed && !state.long_press_sent) {
        // Check for long press
        if ((now - state.press_start_ms) >= LONG_PRESS_MS) {
            state.long_press_sent = true;
            if (callback_) {
                ButtonEvent ev;
                ev.button = state.button;
                ev.type = ButtonEventType::LONG_PRESS;
                ev.timestamp_ms = now;
                callback_(ev);
            }
        }
    } else if (!currently_pressed && state.pressed) {
        // Release detected
        state.pressed = false;
        if (callback_) {
            ButtonEvent ev;
            ev.button = state.button;
            ev.type = ButtonEventType::RELEASE;
            ev.timestamp_ms = now;
            callback_(ev);
        }
    }
}

void InputManager::Poll() {
    // Poll MCP23017 buttons if available
    if (mcp_.IsInitialized()) {
        uint8_t current = mcp_.ReadButtons();
        static uint8_t last_mcp = 0;
        if (current != last_mcp) {
            for (int i = 0; i < 8; i++) {
                bool was_pressed = (last_mcp & (1 << i)) != 0;
                bool is_pressed = (current & (1 << i)) != 0;
                if (was_pressed != is_pressed) {
                    if (callback_) {
                        ButtonEvent ev;
                        ev.button = static_cast<ButtonId>(i);
                        ev.type = is_pressed ? ButtonEventType::PRESS : ButtonEventType::RELEASE;
                        ev.timestamp_ms = esp_timer_get_time() / 1000;
                        callback_(ev);
                    }
                }
            }
            last_mcp = current;
        }
    }

    // Poll GPIO buttons
    for (auto& btn : gpio_buttons_) {
        PollGpioButton(btn);
    }
}

const char* InputManager::ButtonIdToString(ButtonId id) {
    switch (id) {
        case ButtonId::BTN_1:    return "BTN_1";
        case ButtonId::BTN_2:    return "BTN_2";
        case ButtonId::BTN_3:    return "BTN_3";
        case ButtonId::BTN_4:    return "BTN_4";
        case ButtonId::BTN_5:    return "BTN_5";
        case ButtonId::BTN_6:    return "BTN_6";
        case ButtonId::BTN_7:    return "BTN_7";
        case ButtonId::BTN_8:    return "BTN_8";
        case ButtonId::ENTER:    return "ENTER";
        case ButtonId::ESC:      return "ESC";
        case ButtonId::HL_LEFT:  return "HL_LEFT";
        case ButtonId::HL_RIGHT: return "HL_RIGHT";
        default:                 return "UNKNOWN";
    }
}

const char* InputManager::EventTypeToString(ButtonEventType type) {
    switch (type) {
        case ButtonEventType::PRESS:      return "PRESS";
        case ButtonEventType::LONG_PRESS: return "LONG_PRESS";
        case ButtonEventType::RELEASE:    return "RELEASE";
        default:                          return "UNKNOWN";
    }
}
