#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <functional>
#include <cstdint>
#include <string>

#include "mcp23017.h"

enum class ButtonId : uint8_t {
    BTN_1 = 0,
    BTN_2,
    BTN_3,
    BTN_4,
    BTN_5,
    BTN_6,
    BTN_7,
    BTN_8,
    ENTER,
    ESC,
    HL_LEFT,
    HL_RIGHT,
    COUNT
};

enum class ButtonEventType : uint8_t {
    PRESS,
    LONG_PRESS,
    RELEASE
};

struct ButtonEvent {
    ButtonId button;
    ButtonEventType type;
    int64_t timestamp_ms;
};

class InputManager {
public:
    using EventCallback = std::function<void(const ButtonEvent&)>;

    InputManager(i2c_master_bus_handle_t i2c_bus, uint8_t mcp_addr, gpio_num_t mcp_int_pin,
                 gpio_num_t enter_pin, gpio_num_t esc_pin,
                 gpio_num_t hl_left_pin, gpio_num_t hl_right_pin);
    ~InputManager();

    void Init();
    void SetEventCallback(EventCallback callback);

    // Must be called periodically from main loop (checks MCP23017 interrupts + GPIO debouncing)
    void Poll();

    // Convert ButtonId to API string name
    static const char* ButtonIdToString(ButtonId id);

    // Convert ButtonEventType to API string name
    static const char* EventTypeToString(ButtonEventType type);

private:
    Mcp23017 mcp_;
    gpio_num_t enter_pin_;
    gpio_num_t esc_pin_;
    gpio_num_t hl_left_pin_;
    gpio_num_t hl_right_pin_;
    EventCallback callback_;

    // GPIO debounce state
    struct GpioState {
        gpio_num_t pin;
        ButtonId button;
        bool last_level;
        int64_t last_change_ms;
        bool pressed;
        int64_t press_start_ms;
        bool long_press_sent;
    };
    GpioState gpio_buttons_[4];

    static constexpr int DEBOUNCE_MS = 50;
    static constexpr int LONG_PRESS_MS = 1000;

    void InitGpioButton(gpio_num_t pin);
    void PollGpioButton(GpioState& state);
};

#endif // INPUT_MANAGER_H
