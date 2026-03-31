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

enum class AuthStatus {
    PENDING,
    AUTHORIZED,
    REJECTED,
    REVOKED,
    UNKNOWN
};

enum class InputResultStatus {
    NEW_FRAME,
    NO_CHANGE,
    POLL,
    INPUT_ERROR
};

struct DeviceCredentials {
    std::string device_id;
    std::string device_secret;
    std::string refresh_token;
    std::string access_token;
};

struct DeviceStateResponse {
    LlssAction action;
    std::string frame_id;
    std::string active_instance_id;
    int poll_after_ms;
};

struct InputProcessResult {
    InputResultStatus status;
    std::string frame_id;       // When status == NEW_FRAME
    int poll_after_ms;          // When status == POLL
    std::string message;
};

class LlssClient {
public:
    LlssClient(const std::string& server_url);
    ~LlssClient();

    // --- Authentication (new v0.2.1 flow) ---

    // Step 1: Register device (unauthenticated). Returns device_id + device_secret.
    // Device starts as "pending" — admin must authorize before tokens can be obtained.
    bool RegisterDevice(const std::string& hardware_id, const std::string& firmware_version,
                        int display_width, int display_height, int bit_depth, bool partial_refresh,
                        DeviceCredentials& out_credentials);

    // Step 2: Authenticate device and get refresh token.
    // Returns auth_status. If authorized, out_credentials.refresh_token is set.
    AuthStatus AuthenticateDevice(const std::string& hardware_id, const std::string& device_secret,
                                  const std::string& firmware_version,
                                  int display_width, int display_height, int bit_depth,
                                  bool partial_refresh,
                                  DeviceCredentials& out_credentials);

    // Step 3: Exchange refresh token for access token (1-day).
    bool RefreshAccessToken(const std::string& refresh_token, std::string& out_access_token);

    // Step 4: Renew refresh token (call periodically, e.g. every 15 days).
    bool RenewRefreshToken(std::string& out_refresh_token);

    // Check device auth status
    AuthStatus GetAuthStatus();

    // --- Device API (requires access token) ---

    void SetCredentials(const DeviceCredentials& credentials);

    // Poll device state (heartbeat)
    bool GetDeviceState(const std::string& last_frame_id, const std::string& last_event_id,
                        DeviceStateResponse& out_response);

    // Fetch raw framebuffer data for a frame
    // Returns pointer to allocated buffer (caller must free with heap_caps_free) and sets out_len
    uint8_t* FetchFrame(const std::string& frame_id, int& out_len);

    // Send an input event — returns processing result
    bool SendInput(const char* button_name, const char* event_type, InputProcessResult& out_result);

    bool HasCredentials() const { return !credentials_.access_token.empty(); }
    bool HasRefreshToken() const { return !credentials_.refresh_token.empty(); }
    const DeviceCredentials& GetCredentials() const { return credentials_; }

    // Returns true if last HTTP call got 401 (caller should refresh token)
    bool LastCallWas401() const { return last_status_401_; }

private:
    std::string server_url_;
    DeviceCredentials credentials_;
    bool last_status_401_;

    // HTTP helpers
    bool HttpGet(const std::string& path, std::string& out_body);
    bool HttpGetWithStatus(const std::string& path, std::string& out_body, int& out_status);
    bool HttpPost(const std::string& path, const std::string& json_body, std::string& out_body,
                  int& out_status);
    // POST with a specific bearer token (for refresh token auth)
    bool HttpPostWithToken(const std::string& path, const std::string& token,
                           const std::string& json_body, std::string& out_body, int& out_status);
    uint8_t* HttpGetBinary(const std::string& path, int& out_len);

    std::string BuildUrl(const std::string& path);
    static AuthStatus ParseAuthStatus(const char* str);
};

#endif // LLSS_CLIENT_H
