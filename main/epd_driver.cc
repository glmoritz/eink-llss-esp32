#include "epd_driver.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

static const char* TAG = "EpdDriver";

static const int MAX_SPI_TRANSFER = 4096;

EpdDriver::EpdDriver(int width, int height, const EpdSpiConfig& config)
    : width_(width), height_(height), config_(config), spi_(nullptr) {
}

EpdDriver::~EpdDriver() {
    if (spi_) {
        spi_bus_remove_device(spi_);
    }
}

void EpdDriver::SpiGpioInit() {
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (1ULL << config_.rst) | (1ULL << config_.dc) | (1ULL << config_.cs);
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pin_bit_mask = (1ULL << config_.busy);
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    SetRst(1);
}

void EpdDriver::SpiPortInit() {
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num = -1;
    buscfg.mosi_io_num = config_.mosi;
    buscfg.sclk_io_num = config_.scl;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = MAX_SPI_TRANSFER;

    spi_device_interface_config_t devcfg = {};
    devcfg.spics_io_num = -1;
    devcfg.clock_speed_hz = 20 * 1000 * 1000;
    devcfg.mode = 0;
    devcfg.queue_size = 7;

    ESP_ERROR_CHECK(spi_bus_initialize((spi_host_device_t)config_.spi_host, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device((spi_host_device_t)config_.spi_host, &devcfg, &spi_));
}

void EpdDriver::ReadBusy() {
    while (gpio_get_level((gpio_num_t)config_.busy) == 1) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void EpdDriver::SpiSendByte(uint8_t data) {
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &data;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi_, &t));
}

void EpdDriver::SendData(uint8_t data) {
    SetDc(1);
    SetCs(0);
    SpiSendByte(data);
    SetCs(1);
}

void EpdDriver::SendCommand(uint8_t command) {
    SetDc(0);
    SetCs(0);
    SpiSendByte(command);
    SetCs(1);
}

void EpdDriver::WriteBytes(const uint8_t* buffer, int len) {
    SetDc(1);
    SetCs(0);

    int remaining = len;
    int offset = 0;
    while (remaining > 0) {
        int chunk = (remaining > MAX_SPI_TRANSFER) ? MAX_SPI_TRANSFER : remaining;
        spi_transaction_t t = {};
        t.length = 8 * chunk;
        t.tx_buffer = buffer + offset;
        esp_err_t ret = spi_device_polling_transmit(spi_, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transmit failed at offset %d: %s", offset, esp_err_to_name(ret));
            break;
        }
        remaining -= chunk;
        offset += chunk;
    }

    SetCs(1);
}

void EpdDriver::SetWindows(uint16_t xstart, uint16_t ystart, uint16_t xend, uint16_t yend) {
    SendCommand(0x44);
    SendData((xstart * 8) & 0xFF);
    SendData(((xstart * 8) >> 8) & 0xFF);
    SendData((xend * 8) & 0xFF);
    SendData(((xend * 8) >> 8) & 0xFF);

    SendCommand(0x45);
    SendData(yend & 0xFF);
    SendData((yend >> 8) & 0xFF);
    SendData(ystart & 0xFF);
    SendData((ystart >> 8) & 0xFF);
}

void EpdDriver::SetCursor(uint16_t xstart, uint16_t ystart) {
    SendCommand(0x4E);
    SendData((xstart * 8) & 0xFF);
    SendData(((xstart * 8) >> 8) & 0xFF);

    SendCommand(0x4F);
    SendData(ystart & 0xFF);
    SendData((ystart >> 8) & 0xFF);
}

void EpdDriver::TurnOnDisplay() {
    SendCommand(0x22);
    SendData(0xF7);
    SendCommand(0x20);
    ReadBusy();
}

void EpdDriver::TurnOnDisplayPartial() {
    SendCommand(0x22);
    SendData(0xFF);
    SendCommand(0x20);
    ReadBusy();
}

void EpdDriver::Init() {
    ESP_LOGI(TAG, "Initializing EPD %dx%d", width_, height_);

    SpiPortInit();
    SpiGpioInit();

    // Hardware reset
    SetRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    SetRst(0);
    vTaskDelay(pdMS_TO_TICKS(2));
    SetRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ReadBusy();
    SendCommand(0x12);  // SWRESET
    ReadBusy();

    SendCommand(0x18);
    SendData(0x80);

    SendCommand(0x0C);  // Soft start setting
    SendData(0xAE);
    SendData(0xC7);
    SendData(0xC3);
    SendData(0xC0);
    SendData(0x80);

    SendCommand(0x01);  // Driver output control
    SendData((height_ - 1) % 256);
    SendData((height_ - 1) / 256);
    SendData(0x02);

    SendCommand(0x3C);  // Border waveform
    SendData(0x01);

    SendCommand(0x11);  // Data entry mode
    SendData(0x01);

    SendCommand(0x44);  // Set RAM X address
    SendData(0x00);
    SendData(0x00);
    SendData((width_ - 1) % 256);
    SendData((width_ - 1) / 256);

    SendCommand(0x45);  // Set RAM Y address
    SendData((height_ - 1) % 256);
    SendData((height_ - 1) / 256);
    SendData(0x00);
    SendData(0x00);

    SendCommand(0x4E);  // Set RAM X counter
    SendData(0x00);
    SendData(0x00);
    SendCommand(0x4F);  // Set RAM Y counter
    SendData(0x00);
    SendData(0x00);

    ReadBusy();
    ESP_LOGI(TAG, "EPD initialized");
}

void EpdDriver::Clear() {
    ESP_LOGI(TAG, "Clearing display");
    int ps = plane_size();
    uint8_t* white_buf = (uint8_t*)heap_caps_malloc(ps, MALLOC_CAP_SPIRAM);
    if (!white_buf) {
        ESP_LOGE(TAG, "Failed to allocate clear buffer");
        return;
    }
    memset(white_buf, 0xFF, ps);

    SendCommand(0x24);  // MSB plane
    WriteBytes(white_buf, ps);
    SendCommand(0x26);  // LSB plane
    WriteBytes(white_buf, ps);
    TurnOnDisplay();

    heap_caps_free(white_buf);
}

void EpdDriver::Sleep() {
    SendCommand(0x10);  // Enter deep sleep
    SendData(0x01);
    vTaskDelay(pdMS_TO_TICKS(10));
    SetRst(0);
    SetCs(0);
    SetDc(0);
    ESP_LOGI(TAG, "EPD entered sleep mode");
}

void EpdDriver::DisplayFull(const uint8_t* framebuffer, int len) {
    ESP_LOGI(TAG, "Full refresh (1-bit), %d bytes", len);
    SendCommand(0x24);
    WriteBytes(framebuffer, len);
    TurnOnDisplay();
}

void EpdDriver::DisplayPartial(const uint8_t* framebuffer, int len) {
    ESP_LOGI(TAG, "Partial refresh (1-bit), %d bytes", len);
    SendCommand(0x24);
    WriteBytes(framebuffer, len);
    TurnOnDisplayPartial();
}

void EpdDriver::DisplayGrayscaleFull(const uint8_t* framebuffer, int len) {
    int ps = plane_size();
    if (len < ps * 2) {
        ESP_LOGE(TAG, "Grayscale buffer too small: %d < %d", len, ps * 2);
        return;
    }
    ESP_LOGI(TAG, "Full refresh (2-bit grayscale), %d bytes", len);
    SendCommand(0x24);  // MSB plane
    WriteBytes(framebuffer, ps);
    SendCommand(0x26);  // LSB plane
    WriteBytes(framebuffer + ps, ps);
    TurnOnDisplay();
}

void EpdDriver::DisplayGrayscalePartial(const uint8_t* framebuffer, int len) {
    int ps = plane_size();
    if (len < ps * 2) {
        ESP_LOGE(TAG, "Grayscale buffer too small: %d < %d", len, ps * 2);
        return;
    }
    ESP_LOGI(TAG, "Partial refresh (2-bit grayscale), %d bytes", len);
    SendCommand(0x24);  // MSB plane
    WriteBytes(framebuffer, ps);
    SendCommand(0x26);  // LSB plane
    WriteBytes(framebuffer + ps, ps);
    TurnOnDisplayPartial();
}

void EpdDriver::InitPartial() {
    SetRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    SetRst(0);
    vTaskDelay(pdMS_TO_TICKS(2));
    SetRst(1);
    vTaskDelay(pdMS_TO_TICKS(50));

    ReadBusy();

    SendCommand(0x18);
    SendData(0x80);

    SendCommand(0x3C);
    SendData(0x80);

    SetWindows(0, width_ - 1, height_ - 1, 0);
    SetCursor(0, height_ - 1);

    ReadBusy();
    ESP_LOGI(TAG, "Partial refresh mode initialized");
}
