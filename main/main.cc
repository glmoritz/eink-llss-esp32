#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>

#include "llss_application.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi and device credentials
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the LLSS application
    auto& app = LlssApplication::GetInstance();
    app.Initialize();
    app.Run();  // Main event loop, never returns
}
