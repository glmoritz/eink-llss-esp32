#ifndef LLSS_APPLICATION_H
#define LLSS_APPLICATION_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <driver/i2c_master.h>
#include <string>

#include "epd_driver.h"
#include "input_manager.h"
#include "llss_client.h"
#include "llss_state.h"
#include "pmic_axp2101.h"
#include "wifi_provisioning.h"

// Event bits for the main event loop
#define EVT_BUTTON_PRESS     (1 << 0)
#define EVT_WIFI_CONNECTED   (1 << 1)
#define EVT_WIFI_DISCONNECTED (1 << 2)
#define EVT_WIFI_AP_DONE     (1 << 3)
#define EVT_POLL_TIMER       (1 << 4)
#define EVT_SHUTDOWN         (1 << 5)

class LlssApplication {
public:
    static LlssApplication& GetInstance();

    void Initialize();
    void Run();  // Main event loop, never returns

    DeviceState GetDeviceState() const { return state_machine_.GetState(); }

private:
    LlssApplication();
    ~LlssApplication() = default;
    LlssApplication(const LlssApplication&) = delete;
    LlssApplication& operator=(const LlssApplication&) = delete;

    // Hardware
    i2c_master_bus_handle_t i2c_bus_;
    PmicAxp2101* pmic_;
    EpdDriver* epd_;
    InputManager* input_;
    WifiProvisioning wifi_;
    LlssClient* client_;
    LlssStateMachine state_machine_;

    EventGroupHandle_t event_group_;

    // State tracking
    std::string last_frame_id_;
    std::string last_event_id_;
    std::string active_instance_id_;
    int poll_interval_ms_;

    // Pending button event
    ButtonEvent pending_button_event_;
    bool has_pending_button_;

    // Device identity
    std::string hardware_id_;

    void InitI2c();
    void InitPmic();
    void InitEpd();
    void InitInput();
    void InitWifi();

    // State handlers
    void HandleBoot();
    void HandleWifiConfig();
    void HandleWifiConnected();
    void HandleRegistration();
    void HandlePolling();
    void HandleFetchFrame(const std::string& frame_id);
    void HandleSendInput(const ButtonEvent& event);
    void HandleSleep(int duration_ms);

    // Display status messages on the EPD
    void ShowStatusMessage(const char* line1, const char* line2 = nullptr);

    // Load/save device credentials from NVS
    bool LoadCredentials(DeviceCredentials& creds);
    void SaveCredentials(const DeviceCredentials& creds);

    // Generate hardware ID from MAC address
    std::string GenerateHardwareId();
};

#endif // LLSS_APPLICATION_H
