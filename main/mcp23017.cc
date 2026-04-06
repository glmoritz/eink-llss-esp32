#include "mcp23017.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

static const char* TAG = "MCP23017";

Mcp23017::Mcp23017(i2c_master_bus_handle_t i2c_bus, uint8_t addr, gpio_num_t int_pin)
    : i2c_device_(nullptr), int_pin_(int_pin), callback_(nullptr), last_state_(0xFF), initialized_(false) {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = 400000;
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_device_);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add MCP23017 I2C device: %s", esp_err_to_name(ret));
        i2c_device_ = nullptr;
    }
}

Mcp23017::~Mcp23017() {
    if (initialized_ && int_pin_ != GPIO_NUM_NC) {
        gpio_isr_handler_remove(int_pin_);
    }
}

esp_err_t Mcp23017::WriteReg(uint8_t reg, uint8_t value) {
    if (!i2c_device_) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(i2c_device_, buf, 2, 100);
}

esp_err_t Mcp23017::ReadReg(uint8_t reg, uint8_t* value) {
    if (!i2c_device_) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(i2c_device_, &reg, 1, value, 1, 100);
}

esp_err_t Mcp23017::Init() {
    ESP_LOGI(TAG, "Initializing MCP23017");

    if (!i2c_device_) {
        ESP_LOGW(TAG, "MCP23017 I2C device not available, skipping initialization");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;

    // Configure Port A as inputs (8 buttons)
    ret = WriteReg(MCP23017_IODIRA, 0xFF);   // All pins input
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MCP23017 not responding: %s", esp_err_to_name(ret));
        return ret;
    }

    WriteReg(MCP23017_GPPUA, 0xFF);    // Enable pull-ups on all pins
    WriteReg(MCP23017_IPOLA, 0xFF);    // Invert polarity (pressed = 1)

    // Enable interrupts on all Port A pins
    WriteReg(MCP23017_GPINTENA, 0xFF);
    WriteReg(MCP23017_DEFVALA, 0x00);  // Default compare value
    WriteReg(MCP23017_INTCONA, 0x00);  // Interrupt on change from previous

    // Configure IOCON: mirror interrupts, active-low, open-drain
    WriteReg(MCP23017_IOCON, 0x44);    // MIRROR=1, ODR=1

    // Port B unused, set as outputs low
    WriteReg(MCP23017_IODIRB, 0x00);
    WriteReg(MCP23017_GPIOB, 0x00);

    // Clear any pending interrupts by reading
    uint8_t dummy;
    ReadReg(MCP23017_INTCAPA, &dummy);

    // Read initial button state
    ret = ReadReg(MCP23017_GPIOA, &last_state_);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read MCP23017 initial state: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure GPIO interrupt pin if provided
    if (int_pin_ != GPIO_NUM_NC) {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_NEGEDGE;  // Active low interrupt
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << int_pin_);
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        gpio_config(&io_conf);

        gpio_install_isr_service(0);
        gpio_isr_handler_add(int_pin_, GpioIsrHandler, this);
    }

    initialized_ = true;
    ESP_LOGI(TAG, "MCP23017 initialized, initial state: 0x%02X", last_state_);
    return ESP_OK;
}

uint8_t Mcp23017::ReadButtons() {
    if (!initialized_) return 0;
    uint8_t value = 0;
    ReadReg(MCP23017_GPIOA, &value);
    return value;
}

void Mcp23017::SetCallback(ButtonCallback callback) {
    callback_ = callback;
}

void Mcp23017::HandleInterrupt() {
    if (!initialized_) return;

    // Read interrupt flags to see which pins changed
    uint8_t intf = 0;
    ReadReg(MCP23017_INTFA, &intf);
    // Read captured state at time of interrupt
    uint8_t intcap = 0;
    ReadReg(MCP23017_INTCAPA, &intcap);
    // Read current state
    uint8_t current = 0;
    ReadReg(MCP23017_GPIOA, &current);

    if (callback_) {
        for (int i = 0; i < 8; i++) {
            if (intf & (1 << i)) {
                bool pressed = (current & (1 << i)) != 0;
                callback_(i, pressed);
            }
        }
    }

    last_state_ = current;
}

void IRAM_ATTR Mcp23017::GpioIsrHandler(void* arg) {
    // In ISR context - we need to defer to a task
    // The application should poll HandleInterrupt() or use a task notification
    // This is kept minimal for ISR safety
    auto* self = static_cast<Mcp23017*>(arg);
    (void)self;
    // The main loop will detect the interrupt via the int pin level
}
