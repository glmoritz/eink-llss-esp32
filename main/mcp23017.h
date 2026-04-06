#ifndef MCP23017_H
#define MCP23017_H

#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <functional>
#include <cstdint>

// MCP23017 Register addresses (BANK=0 mode)
#define MCP23017_IODIRA   0x00
#define MCP23017_IODIRB   0x01
#define MCP23017_IPOLA    0x02
#define MCP23017_IPOLB    0x03
#define MCP23017_GPINTENA 0x04
#define MCP23017_GPINTENB 0x05
#define MCP23017_DEFVALA  0x06
#define MCP23017_DEFVALB  0x07
#define MCP23017_INTCONA  0x08
#define MCP23017_INTCONB  0x09
#define MCP23017_IOCON    0x0A
#define MCP23017_GPPUA    0x0C
#define MCP23017_GPPUB    0x0D
#define MCP23017_INTFA    0x0E
#define MCP23017_INTFB    0x0F
#define MCP23017_INTCAPA  0x10
#define MCP23017_INTCAPB  0x11
#define MCP23017_GPIOA    0x12
#define MCP23017_GPIOB    0x13

class Mcp23017 {
public:
    using ButtonCallback = std::function<void(uint8_t button_index, bool pressed)>;

    Mcp23017(i2c_master_bus_handle_t i2c_bus, uint8_t addr, gpio_num_t int_pin);
    ~Mcp23017();

    // Initialize the MCP23017 for 8 buttons on port A with interrupts
    // Returns ESP_OK on success, error code on failure
    esp_err_t Init();

    // Check if device was successfully initialized
    bool IsInitialized() const { return initialized_; }

    // Read all 8 button states (bit per button, 1 = pressed)
    uint8_t ReadButtons();

    // Set callback for button state changes
    void SetCallback(ButtonCallback callback);

    // Poll for changes (call from task or ISR handler)
    void HandleInterrupt();

private:
    i2c_master_dev_handle_t i2c_device_;
    gpio_num_t int_pin_;
    ButtonCallback callback_;
    uint8_t last_state_;
    bool initialized_;

    esp_err_t WriteReg(uint8_t reg, uint8_t value);
    esp_err_t ReadReg(uint8_t reg, uint8_t* value);

    static void IRAM_ATTR GpioIsrHandler(void* arg);
};

#endif // MCP23017_H
