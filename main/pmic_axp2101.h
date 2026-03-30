#ifndef PMIC_AXP2101_H
#define PMIC_AXP2101_H

#include <driver/i2c_master.h>
#include <cstdint>

class PmicAxp2101 {
public:
    PmicAxp2101(i2c_master_bus_handle_t i2c_bus, uint8_t addr);

    bool IsCharging();
    bool IsDischarging();
    int GetBatteryLevel();
    void PowerOff();

private:
    i2c_master_dev_handle_t i2c_device_;

    void WriteReg(uint8_t reg, uint8_t value);
    uint8_t ReadReg(uint8_t reg);
    int GetBatteryCurrentDirection();
};

#endif // PMIC_AXP2101_H
