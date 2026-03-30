#ifndef EPD_DRIVER_H
#define EPD_DRIVER_H

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <cstdint>
#include <cstring>

struct EpdSpiConfig {
    uint8_t cs;
    uint8_t dc;
    uint8_t rst;
    uint8_t busy;
    uint8_t mosi;
    uint8_t scl;
    int spi_host;
};

class EpdDriver {
public:
    EpdDriver(int width, int height, const EpdSpiConfig& config);
    ~EpdDriver();

    void Init();
    void Clear();
    void Sleep();

    // Write raw 1-bit packed framebuffer to display (full refresh)
    void DisplayFull(const uint8_t* framebuffer, int len);

    // Write raw 1-bit packed framebuffer to display (partial refresh)
    void DisplayPartial(const uint8_t* framebuffer, int len);

    // Initialize partial refresh mode
    void InitPartial();

    int width() const { return width_; }
    int height() const { return height_; }
    int buffer_size() const { return width_ * height_ / 8; }

private:
    const int width_;
    const int height_;
    const EpdSpiConfig config_;
    spi_device_handle_t spi_;

    void SpiGpioInit();
    void SpiPortInit();
    void ReadBusy();

    void SetCs(int level) { gpio_set_level((gpio_num_t)config_.cs, level); }
    void SetDc(int level) { gpio_set_level((gpio_num_t)config_.dc, level); }
    void SetRst(int level) { gpio_set_level((gpio_num_t)config_.rst, level); }

    void SpiSendByte(uint8_t data);
    void SendData(uint8_t data);
    void SendCommand(uint8_t command);
    void WriteBytes(const uint8_t* buffer, int len);
    void SetWindows(uint16_t xstart, uint16_t ystart, uint16_t xend, uint16_t yend);
    void SetCursor(uint16_t xstart, uint16_t ystart);
    void TurnOnDisplay();
    void TurnOnDisplayPartial();
};

#endif // EPD_DRIVER_H
