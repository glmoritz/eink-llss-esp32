#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <esp_event.h>
#include <string>
#include <functional>

enum class WifiProvState {
    IDLE,
    AP_ACTIVE,
    CONNECTING,
    CONNECTED,
    DISCONNECTED
};

class WifiProvisioning {
public:
    using StateCallback = std::function<void(WifiProvState state, const std::string& info)>;

    WifiProvisioning();
    ~WifiProvisioning();

    // Initialize WiFi subsystem
    void Init();

    // Check if we have stored WiFi credentials
    bool HasStoredCredentials();

    // Try connecting with stored credentials
    void ConnectStored();

    // Start AP mode for provisioning (captive portal)
    void StartAP(const std::string& ssid, const std::string& password = "");

    // Stop AP mode
    void StopAP();

    // Check connection status
    bool IsConnected() const;

    // Get IP address
    std::string GetIpAddress() const;

    // Get connected SSID
    std::string GetSsid() const;

    // Set state change callback
    void SetCallback(StateCallback callback) { callback_ = callback; }

    // Store WiFi credentials (called from captive portal HTTP handler)
    void StoreCredentials(const std::string& ssid, const std::string& password);

private:
    StateCallback callback_;
    bool connected_;
    std::string ip_address_;
    std::string ssid_;

    static void WifiEventHandler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

    void StartHttpServer();
    void StopHttpServer();

    void* http_server_;  // httpd_handle_t
};

#endif // WIFI_PROVISIONING_H
