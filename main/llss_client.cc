#include "llss_client.h"

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_tls.h>
#include <esp_crt_bundle.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <cstring>
#include <esp_heap_caps.h>

static const char* TAG = "LlssClient";

// Buffer for HTTP response accumulation
struct HttpBuffer {
    uint8_t* data;
    int len;
    int capacity;
    bool is_binary;
};

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    auto* buf = static_cast<HttpBuffer*>(evt->user_data);
    if (!buf) return ESP_OK;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (buf->len + evt->data_len > buf->capacity) {
                int new_cap = (buf->len + evt->data_len) * 2;
                uint8_t* new_data = (uint8_t*)heap_caps_realloc(buf->data, new_cap,
                                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!new_data) {
                    ESP_LOGE(TAG, "Failed to realloc HTTP buffer to %d", new_cap);
                    return ESP_FAIL;
                }
                buf->data = new_data;
                buf->capacity = new_cap;
            }
            memcpy(buf->data + buf->len, evt->data, evt->data_len);
            buf->len += evt->data_len;
            break;
        default:
            break;
    }
    return ESP_OK;
}

LlssClient::LlssClient(const std::string& server_url)
    : server_url_(server_url), last_status_401_(false) {
}

LlssClient::~LlssClient() {
}

std::string LlssClient::BuildUrl(const std::string& path) {
    return server_url_ + path;
}

void LlssClient::SetCredentials(const DeviceCredentials& credentials) {
    credentials_ = credentials;
}

bool LlssClient::HttpGet(const std::string& path, std::string& out_body) {
    int status;
    return HttpGetWithStatus(path, out_body, status);
}

bool LlssClient::HttpGetWithStatus(const std::string& path, std::string& out_body, int& out_status) {
    std::string url = BuildUrl(path);
    last_status_401_ = false;

    HttpBuffer buf = {};
    buf.capacity = 4096;
    buf.data = (uint8_t*)heap_caps_malloc(buf.capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf.data) {
        ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = http_event_handler;
    config.user_data = &buf;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 15000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        heap_caps_free(buf.data);
        return false;
    }

    if (!credentials_.access_token.empty()) {
        std::string auth = "Bearer " + credentials_.access_token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }

    esp_err_t err = esp_http_client_perform(client);
    out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (out_status == 401) {
        last_status_401_ = true;
    }

    if (err != ESP_OK || out_status < 200 || out_status >= 300) {
        ESP_LOGE(TAG, "GET %s failed: err=%s, status=%d", path.c_str(), esp_err_to_name(err), out_status);
        heap_caps_free(buf.data);
        return false;
    }

    out_body.assign((char*)buf.data, buf.len);
    heap_caps_free(buf.data);
    return true;
}

bool LlssClient::HttpPost(const std::string& path, const std::string& json_body,
                           std::string& out_body, int& out_status) {
    return HttpPostWithToken(path, credentials_.access_token, json_body, out_body, out_status);
}

bool LlssClient::HttpPostWithToken(const std::string& path, const std::string& token,
                                    const std::string& json_body, std::string& out_body,
                                    int& out_status) {
    std::string url = BuildUrl(path);
    last_status_401_ = false;

    HttpBuffer buf = {};
    buf.capacity = 4096;
    buf.data = (uint8_t*)heap_caps_malloc(buf.capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf.data) {
        ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = http_event_handler;
    config.user_data = &buf;
    config.method = HTTP_METHOD_POST;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 15000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        heap_caps_free(buf.data);
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");

    if (!token.empty()) {
        std::string auth = "Bearer " + token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }

    esp_http_client_set_post_field(client, json_body.c_str(), json_body.length());

    esp_err_t err = esp_http_client_perform(client);
    out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (out_status == 401) {
        last_status_401_ = true;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "POST %s failed: %s", path.c_str(), esp_err_to_name(err));
        heap_caps_free(buf.data);
        return false;
    }

    out_body.assign((char*)buf.data, buf.len);
    heap_caps_free(buf.data);
    return true;
}

uint8_t* LlssClient::HttpGetBinary(const std::string& path, int& out_len) {
    std::string url = BuildUrl(path);
    last_status_401_ = false;

    HttpBuffer buf = {};
    buf.capacity = 131072;  // Start with 128KB for grayscale framebuffer
    buf.data = (uint8_t*)heap_caps_malloc(buf.capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf.data) {
        ESP_LOGE(TAG, "Failed to allocate binary HTTP buffer");
        out_len = 0;
        return nullptr;
    }

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.event_handler = http_event_handler;
    config.user_data = &buf;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 30000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        heap_caps_free(buf.data);
        out_len = 0;
        return nullptr;
    }

    if (!credentials_.access_token.empty()) {
        std::string auth = "Bearer " + credentials_.access_token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        if (status == 401) last_status_401_ = true;
        ESP_LOGE(TAG, "GET binary %s failed: err=%s, status=%d", path.c_str(),
                 esp_err_to_name(err), status);
        heap_caps_free(buf.data);
        out_len = 0;
        return nullptr;
    }

    out_len = buf.len;
    return buf.data;  // Caller must free with heap_caps_free
}

AuthStatus LlssClient::ParseAuthStatus(const char* str) {
    if (!str) return AuthStatus::UNKNOWN;
    if (strcmp(str, "pending") == 0) return AuthStatus::PENDING;
    if (strcmp(str, "authorized") == 0) return AuthStatus::AUTHORIZED;
    if (strcmp(str, "rejected") == 0) return AuthStatus::REJECTED;
    if (strcmp(str, "revoked") == 0) return AuthStatus::REVOKED;
    return AuthStatus::UNKNOWN;
}

static cJSON* CreateDisplayJson(int width, int height, int bit_depth, bool partial_refresh) {
    cJSON* display = cJSON_CreateObject();
    cJSON_AddNumberToObject(display, "width", width);
    cJSON_AddNumberToObject(display, "height", height);
    cJSON_AddNumberToObject(display, "bit_depth", bit_depth);
    cJSON_AddBoolToObject(display, "partial_refresh", partial_refresh);
    return display;
}

bool LlssClient::RegisterDevice(const std::string& hardware_id, const std::string& firmware_version,
                                  int display_width, int display_height, int bit_depth,
                                  bool partial_refresh, DeviceCredentials& out_credentials) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "hardware_id", hardware_id.c_str());
    cJSON_AddStringToObject(root, "firmware_version", firmware_version.c_str());
    cJSON_AddItemToObject(root, "display",
                          CreateDisplayJson(display_width, display_height, bit_depth, partial_refresh));

    char* json_str = cJSON_PrintUnformatted(root);
    std::string body(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    std::string response;
    int status;
    // Use the new auth endpoint — unauthenticated
    if (!HttpPostWithToken("/auth/devices/register", "", body, response, status)) {
        return false;
    }

    if (status == 409) {
        ESP_LOGW(TAG, "Device already registered");
        // Device exists — not an error, caller should proceed to authenticate
        return false;
    }

    if (status != 201) {
        ESP_LOGE(TAG, "Registration failed with status %d: %s", status, response.c_str());
        return false;
    }

    cJSON* resp = cJSON_Parse(response.c_str());
    if (!resp) {
        ESP_LOGE(TAG, "Failed to parse registration response");
        return false;
    }

    cJSON* device_id = cJSON_GetObjectItem(resp, "device_id");
    cJSON* device_secret = cJSON_GetObjectItem(resp, "device_secret");
    cJSON* auth_status = cJSON_GetObjectItem(resp, "auth_status");

    if (device_id && device_id->valuestring) {
        out_credentials.device_id = device_id->valuestring;
    }
    if (device_secret && device_secret->valuestring) {
        out_credentials.device_secret = device_secret->valuestring;
    }

    cJSON_Delete(resp);

    ESP_LOGI(TAG, "Device registered: %s (status: %s)",
             out_credentials.device_id.c_str(),
             (auth_status && auth_status->valuestring) ? auth_status->valuestring : "unknown");
    return !out_credentials.device_id.empty();
}

AuthStatus LlssClient::AuthenticateDevice(const std::string& hardware_id,
                                            const std::string& device_secret,
                                            const std::string& firmware_version,
                                            int display_width, int display_height,
                                            int bit_depth, bool partial_refresh,
                                            DeviceCredentials& out_credentials) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "hardware_id", hardware_id.c_str());
    cJSON_AddStringToObject(root, "device_secret", device_secret.c_str());
    cJSON_AddStringToObject(root, "firmware_version", firmware_version.c_str());
    cJSON_AddItemToObject(root, "display",
                          CreateDisplayJson(display_width, display_height, bit_depth, partial_refresh));

    char* json_str = cJSON_PrintUnformatted(root);
    std::string body(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    std::string response;
    int status;
    // Unauthenticated endpoint
    if (!HttpPostWithToken("/auth/devices/token", "", body, response, status)) {
        return AuthStatus::UNKNOWN;
    }

    if (status == 401) return AuthStatus::UNKNOWN;
    if (status == 403) return AuthStatus::REJECTED;

    cJSON* resp = cJSON_Parse(response.c_str());
    if (!resp) {
        ESP_LOGE(TAG, "Failed to parse auth response");
        return AuthStatus::UNKNOWN;
    }

    cJSON* auth_status_json = cJSON_GetObjectItem(resp, "auth_status");
    AuthStatus auth_status = ParseAuthStatus(
        auth_status_json ? auth_status_json->valuestring : nullptr);

    cJSON* device_id = cJSON_GetObjectItem(resp, "device_id");
    if (device_id && device_id->valuestring) {
        out_credentials.device_id = device_id->valuestring;
    }

    cJSON* refresh_token = cJSON_GetObjectItem(resp, "refresh_token");
    if (refresh_token && refresh_token->valuestring && strlen(refresh_token->valuestring) > 0) {
        out_credentials.refresh_token = refresh_token->valuestring;
    }

    cJSON* message = cJSON_GetObjectItem(resp, "message");
    if (message && message->valuestring) {
        ESP_LOGI(TAG, "Auth response: %s", message->valuestring);
    }

    cJSON_Delete(resp);
    return auth_status;
}

bool LlssClient::RefreshAccessToken(const std::string& refresh_token, std::string& out_access_token) {
    std::string response;
    int status;
    // POST with refresh token in Authorization header, empty body
    if (!HttpPostWithToken("/auth/devices/refresh", refresh_token, "{}", response, status)) {
        return false;
    }

    if (status == 401 || status == 403) {
        ESP_LOGE(TAG, "Refresh token invalid/expired (status %d)", status);
        return false;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Token refresh failed: status %d", status);
        return false;
    }

    cJSON* resp = cJSON_Parse(response.c_str());
    if (!resp) {
        ESP_LOGE(TAG, "Failed to parse token refresh response");
        return false;
    }

    cJSON* access_token = cJSON_GetObjectItem(resp, "access_token");
    if (access_token && access_token->valuestring) {
        out_access_token = access_token->valuestring;
        credentials_.access_token = out_access_token;
        ESP_LOGI(TAG, "Access token refreshed");
    }

    cJSON_Delete(resp);
    return !out_access_token.empty();
}

bool LlssClient::RenewRefreshToken(std::string& out_refresh_token) {
    std::string response;
    int status;
    // Uses access token
    if (!HttpPost("/auth/devices/renew-refresh", "{}", response, status)) {
        return false;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Refresh token renewal failed: status %d", status);
        return false;
    }

    cJSON* resp = cJSON_Parse(response.c_str());
    if (!resp) return false;

    cJSON* refresh_token = cJSON_GetObjectItem(resp, "refresh_token");
    if (refresh_token && refresh_token->valuestring) {
        out_refresh_token = refresh_token->valuestring;
        credentials_.refresh_token = out_refresh_token;
        ESP_LOGI(TAG, "Refresh token renewed");
    }

    cJSON_Delete(resp);
    return !out_refresh_token.empty();
}

AuthStatus LlssClient::GetAuthStatus() {
    std::string response;
    int status;
    if (!HttpGetWithStatus("/auth/devices/status", response, status)) {
        return AuthStatus::UNKNOWN;
    }

    cJSON* resp = cJSON_Parse(response.c_str());
    if (!resp) return AuthStatus::UNKNOWN;

    cJSON* auth_status = cJSON_GetObjectItem(resp, "auth_status");
    AuthStatus result = ParseAuthStatus(auth_status ? auth_status->valuestring : nullptr);
    cJSON_Delete(resp);
    return result;
}

bool LlssClient::GetDeviceState(const std::string& last_frame_id, const std::string& last_event_id,
                                  DeviceStateResponse& out_response) {
    std::string path = "/devices/" + credentials_.device_id + "/state";

    bool has_param = false;
    if (!last_frame_id.empty()) {
        path += "?last_frame_id=" + last_frame_id;
        has_param = true;
    }
    if (!last_event_id.empty()) {
        path += (has_param ? "&" : "?");
        path += "last_event_id=" + last_event_id;
    }

    std::string response;
    if (!HttpGet(path, response)) {
        out_response.action = LlssAction::ERROR;
        return false;
    }

    cJSON* resp = cJSON_Parse(response.c_str());
    if (!resp) {
        ESP_LOGE(TAG, "Failed to parse state response");
        out_response.action = LlssAction::ERROR;
        return false;
    }

    cJSON* action = cJSON_GetObjectItem(resp, "action");
    if (action && action->valuestring) {
        if (strcmp(action->valuestring, "FETCH_FRAME") == 0) {
            out_response.action = LlssAction::FETCH_FRAME;
        } else if (strcmp(action->valuestring, "SLEEP") == 0) {
            out_response.action = LlssAction::SLEEP;
        } else {
            out_response.action = LlssAction::NOOP;
        }
    } else {
        out_response.action = LlssAction::NOOP;
    }

    cJSON* frame_id = cJSON_GetObjectItem(resp, "frame_id");
    if (frame_id && frame_id->valuestring) {
        out_response.frame_id = frame_id->valuestring;
    }

    cJSON* instance_id = cJSON_GetObjectItem(resp, "active_instance_id");
    if (instance_id && instance_id->valuestring) {
        out_response.active_instance_id = instance_id->valuestring;
    }

    cJSON* poll_ms = cJSON_GetObjectItem(resp, "poll_after_ms");
    if (poll_ms && cJSON_IsNumber(poll_ms)) {
        out_response.poll_after_ms = poll_ms->valueint;
    } else {
        out_response.poll_after_ms = 5000;
    }

    cJSON_Delete(resp);
    return true;
}

uint8_t* LlssClient::FetchFrame(const std::string& frame_id, int& out_len) {
    std::string path = "/devices/" + credentials_.device_id + "/frames/" + frame_id;
    return HttpGetBinary(path, out_len);
}

bool LlssClient::SendInput(const char* button_name, const char* event_type,
                             InputProcessResult& out_result) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "button", button_name);
    cJSON_AddStringToObject(root, "event_type", event_type);

    // ISO 8601 timestamp (simplified - epoch ms)
    char ts[32];
    int64_t now_us = esp_timer_get_time();
    snprintf(ts, sizeof(ts), "%lld", now_us / 1000);
    cJSON_AddStringToObject(root, "timestamp", ts);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string body(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    std::string path = "/devices/" + credentials_.device_id + "/inputs";
    std::string response;
    int status;
    bool ok = HttpPost(path, body, response, status);

    if (!ok || status < 200 || status >= 300) {
        ESP_LOGE(TAG, "SendInput failed: status=%d", status);
        out_result.status = InputResultStatus::INPUT_ERROR;
        return false;
    }

    // Parse InputProcessResponse
    out_result.status = InputResultStatus::NO_CHANGE;
    out_result.poll_after_ms = 0;

    cJSON* resp = cJSON_Parse(response.c_str());
    if (resp) {
        cJSON* result_status = cJSON_GetObjectItem(resp, "status");
        if (result_status && result_status->valuestring) {
            if (strcmp(result_status->valuestring, "NEW_FRAME") == 0) {
                out_result.status = InputResultStatus::NEW_FRAME;
            } else if (strcmp(result_status->valuestring, "POLL") == 0) {
                out_result.status = InputResultStatus::POLL;
            } else if (strcmp(result_status->valuestring, "ERROR") == 0) {
                out_result.status = InputResultStatus::INPUT_ERROR;
            }
        }

        cJSON* frame_id = cJSON_GetObjectItem(resp, "frame_id");
        if (frame_id && frame_id->valuestring) {
            out_result.frame_id = frame_id->valuestring;
        }

        cJSON* poll_ms = cJSON_GetObjectItem(resp, "poll_after_ms");
        if (poll_ms && cJSON_IsNumber(poll_ms)) {
            out_result.poll_after_ms = poll_ms->valueint;
        }

        cJSON* message = cJSON_GetObjectItem(resp, "message");
        if (message && message->valuestring) {
            out_result.message = message->valuestring;
        }

        cJSON_Delete(resp);
    }

    ESP_LOGI(TAG, "Input sent: %s %s", button_name, event_type);
    return true;
}
