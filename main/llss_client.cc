#include "llss_client.h"

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_tls.h>
#include <esp_crt_bundle.h>
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

LlssClient::LlssClient(const std::string& server_url) : server_url_(server_url) {
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
    std::string url = BuildUrl(path);

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
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGE(TAG, "GET %s failed: err=%s, status=%d", path.c_str(), esp_err_to_name(err), status);
        heap_caps_free(buf.data);
        return false;
    }

    out_body.assign((char*)buf.data, buf.len);
    heap_caps_free(buf.data);
    return true;
}

bool LlssClient::HttpPost(const std::string& path, const std::string& json_body,
                           std::string& out_body, int& out_status) {
    std::string url = BuildUrl(path);

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

    if (!credentials_.access_token.empty()) {
        std::string auth = "Bearer " + credentials_.access_token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }

    esp_http_client_set_post_field(client, json_body.c_str(), json_body.length());

    esp_err_t err = esp_http_client_perform(client);
    out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

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

    HttpBuffer buf = {};
    buf.capacity = 65536;  // Start with 64KB for framebuffer
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
        ESP_LOGE(TAG, "GET binary %s failed: err=%s, status=%d", path.c_str(),
                 esp_err_to_name(err), status);
        heap_caps_free(buf.data);
        out_len = 0;
        return nullptr;
    }

    out_len = buf.len;
    return buf.data;  // Caller must free with heap_caps_free
}

bool LlssClient::RegisterDevice(const std::string& hardware_id, const std::string& firmware_version,
                                  int display_width, int display_height, int bit_depth,
                                  bool partial_refresh, DeviceCredentials& out_credentials) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "hardware_id", hardware_id.c_str());
    cJSON_AddStringToObject(root, "firmware_version", firmware_version.c_str());

    cJSON* display = cJSON_CreateObject();
    cJSON_AddNumberToObject(display, "width", display_width);
    cJSON_AddNumberToObject(display, "height", display_height);
    cJSON_AddNumberToObject(display, "bit_depth", bit_depth);
    cJSON_AddBoolToObject(display, "partial_refresh", partial_refresh);
    cJSON_AddItemToObject(root, "display", display);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string body(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    std::string response;
    int status;
    if (!HttpPost("/devices/register", body, response, status)) {
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
    cJSON* access_token = cJSON_GetObjectItem(resp, "access_token");

    if (device_id && device_id->valuestring) {
        out_credentials.device_id = device_id->valuestring;
    }
    if (device_secret && device_secret->valuestring) {
        out_credentials.device_secret = device_secret->valuestring;
    }
    if (access_token && access_token->valuestring) {
        out_credentials.access_token = access_token->valuestring;
    }

    cJSON_Delete(resp);

    ESP_LOGI(TAG, "Device registered: %s", out_credentials.device_id.c_str());
    return !out_credentials.device_id.empty();
}

bool LlssClient::GetDeviceState(const std::string& last_frame_id, const std::string& last_event_id,
                                  DeviceStateResponse& out_response) {
    std::string path = "/devices/" + credentials_.device_id + "/state";

    // Add query parameters
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

bool LlssClient::SendInput(const char* button_name, const char* event_type) {
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
        return false;
    }

    ESP_LOGI(TAG, "Input sent: %s %s", button_name, event_type);
    return true;
}
