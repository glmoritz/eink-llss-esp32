#ifndef LLSS_CLIENT_H
#define LLSS_CLIENT_H

#include <string>
#include <cstdint>
#include <functional>

enum class LlssAction {
    NOOP,
    FETCH_FRAME,
    SLEEP,
    ERROR
};

struct DeviceCredentials {
    std::string device_id;
    std::string device_secret;
    std::string access_token;
};

struct DeviceStateResponse {
    LlssAction action;
    std::string frame_id;
    std::string active_instance_id;
    int poll_after_ms;
};

class LlssClient {
public:
    LlssClient(const std::string& server_url);
    ~LlssClient();

    // Register a new device, returns credentials
    bool RegisterDevice(const std::string& hardware_id, const std::string& firmware_version,
                        int display_width, int display_height, int bit_depth, bool partial_refresh,
                        DeviceCredentials& out_credentials);

    // Set credentials for authenticated requests
    void SetCredentials(const DeviceCredentials& credentials);

    // Poll device state (heartbeat)
    bool GetDeviceState(const std::string& last_frame_id, const std::string& last_event_id,
                        DeviceStateResponse& out_response);

    // Fetch raw framebuffer data for a frame
    // Returns pointer to allocated buffer (caller must free with heap_caps_free) and sets out_len
    uint8_t* FetchFrame(const std::string& frame_id, int& out_len);

    // Send an input event
    bool SendInput(const char* button_name, const char* event_type);

    bool HasCredentials() const { return !credentials_.access_token.empty(); }

private:
    std::string server_url_;
    DeviceCredentials credentials_;

    // HTTP helper - performs a request and returns response body
    // For binary responses, use FetchBinary
    bool HttpGet(const std::string& path, std::string& out_body);
    bool HttpPost(const std::string& path, const std::string& json_body, std::string& out_body,
                  int& out_status);
    uint8_t* HttpGetBinary(const std::string& path, int& out_len);

    std::string BuildUrl(const std::string& path);
};

#endif // LLSS_CLIENT_H
