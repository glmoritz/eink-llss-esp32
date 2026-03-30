#include "pmic_axp2101.h"

#include <esp_log.h>
#include <cstring>

static const char* TAG = "PMIC";

PmicAxp2101::PmicAxp2101(i2c_master_bus_handle_t i2c_bus, uint8_t addr) {
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &i2c_device_));

    ESP_LOGI(TAG, "Initializing AXP2101");

    // Power-on config
    WriteReg(0x22, 0b110);   // PWRON > OFFLEVEL as POWEROFF Source enable
    WriteReg(0x27, 0x10);    // Hold 4s to power off

    // Disable All DCs but DC1
    WriteReg(0x80, 0x01);
    // Disable All LDOs
    WriteReg(0x90, 0x00);
    WriteReg(0x91, 0x00);

    // Set DC1 to 3.3V
    WriteReg(0x82, (3300 - 1500) / 100);

    // Set ALDO1/2/3 to 3.3V
    WriteReg(0x92, (3300 - 500) / 100);
    WriteReg(0x93, (3300 - 500) / 100);
    WriteReg(0x94, (3300 - 500) / 100);

    // Enable ALDO1, ALDO2, ALDO3
    WriteReg(0x90, 0x07);

    // Charger configuration
    WriteReg(0x64, 0x03);  // CV charger voltage 4.2V
    WriteReg(0x61, 0x02);  // Precharge current 50mA
    WriteReg(0x62, 0x08);  // Charger current 200mA
    WriteReg(0x63, 0x01);  // Term charge current 25mA
}

void PmicAxp2101::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(i2c_device_, buf, 2, 100));
}

uint8_t PmicAxp2101::ReadReg(uint8_t reg) {
    uint8_t value = 0;
    ESP_ERROR_CHECK(i2c_master_transmit_receive(i2c_device_, &reg, 1, &value, 1, 100));
    return value;
}

int PmicAxp2101::GetBatteryCurrentDirection() {
    return (ReadReg(0x00) >> 5) & 0x03;
}

bool PmicAxp2101::IsCharging() {
    return GetBatteryCurrentDirection() == 1;
}

bool PmicAxp2101::IsDischarging() {
    return GetBatteryCurrentDirection() == 2;
}

int PmicAxp2101::GetBatteryLevel() {
    return ReadReg(0xA4);
}

void PmicAxp2101::PowerOff() {
    ESP_LOGI(TAG, "Powering off");
    WriteReg(0x10, ReadReg(0x10) | 0x01);
}
