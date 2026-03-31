#ifndef LLSS_CONFIG_H
#define LLSS_CONFIG_H

#include <driver/gpio.h>

// -- EPD SPI Pins (from waveshare 3.97 board) --
#define EPD_SPI_NUM        SPI3_HOST
#define EPD_DC_PIN    GPIO_NUM_9
#define EPD_CS_PIN    GPIO_NUM_10
#define EPD_SCK_PIN   GPIO_NUM_11
#define EPD_MOSI_PIN  GPIO_NUM_12
#define EPD_RST_PIN   GPIO_NUM_46
#define EPD_BUSY_PIN  GPIO_NUM_3

#define EPD_WIDTH   800
#define EPD_HEIGHT  480
#define EPD_BIT_DEPTH 2
#define EPD_PLANE_SIZE (EPD_WIDTH * EPD_HEIGHT / 8)        // 48000 bytes (one 1-bit plane)
#define EPD_GRAYSCALE_BUFFER_SIZE (EPD_PLANE_SIZE * 2)     // 96000 bytes (MSB + LSB planes)

// -- AXP2101 PMIC --
#define PMIC_I2C_SDA_PIN  GPIO_NUM_41
#define PMIC_I2C_SCL_PIN  GPIO_NUM_42
#define PMIC_I2C_ADDR     0x34

// -- MCP23017 I2C Button Matrix (8 bottom buttons) --
#define MCP23017_I2C_ADDR     0x20
#define MCP23017_INT_PIN      GPIO_NUM_4  // Interrupt pin from MCP23017

// -- Direct GPIO Buttons --
#define BOOT_BUTTON_GPIO    GPIO_NUM_0
#define PWR_BUTTON_GPIO     GPIO_NUM_1
#define ENTER_BUTTON_GPIO   GPIO_NUM_5
#define ESC_BUTTON_GPIO     GPIO_NUM_6
#define HL_LEFT_BUTTON_GPIO GPIO_NUM_7
#define HL_RIGHT_BUTTON_GPIO GPIO_NUM_8

// -- LLSS Server --
#define LLSS_SERVER_URL     "https://eink.tutu.eng.br/api"
#define LLSS_FIRMWARE_VERSION "0.1.0"

// -- Polling --
#define LLSS_DEFAULT_POLL_MS  5000
#define LLSS_MIN_POLL_MS      1000
#define LLSS_MAX_POLL_MS      60000

// -- WiFi AP Config --
#define WIFI_AP_SSID_PREFIX "LLSS-"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    2

// -- NVS Namespaces --
#define NVS_NAMESPACE_DEVICE  "llss_device"
#define NVS_NAMESPACE_WIFI    "llss_wifi"

#endif // LLSS_CONFIG_H
