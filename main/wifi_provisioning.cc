#include "wifi_provisioning.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_http_server.h>
#include <nvs_flash.h>
#include <cstring>
#include <algorithm>

#include "llss_config.h"
#include "settings.h"

static const char* TAG = "WifiProv";

// HTML page for WiFi configuration (minimal, works on mobile)
static const char WIFI_CONFIG_HTML[] = R"html(
<!DOCTYPE html>
<html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LLSS WiFi Setup</title>
<style>
body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 20px;background:#f5f5f5}
h1{color:#333;font-size:1.5em}
input,select{width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border:1px solid #ccc;border-radius:4px}
button{width:100%;padding:14px;background:#2196F3;color:white;border:none;border-radius:4px;cursor:pointer;font-size:1em}
button:hover{background:#1976D2}
.msg{padding:10px;margin:10px 0;border-radius:4px}
.ok{background:#c8e6c9;color:#2e7d32}
.err{background:#ffcdd2;color:#c62828}
</style></head><body>
<h1>LLSS WiFi Setup</h1>
<form method="POST" action="/wifi">
<label>SSID:</label>
<input name="ssid" required maxlength="32">
<label>Password:</label>
<input name="password" type="password" maxlength="64">
<button type="submit">Connect</button>
</form>
</body></html>
)html";

static const char WIFI_OK_HTML[] = R"html(
<!DOCTYPE html>
<html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LLSS WiFi Setup</title>
<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 20px}</style>
</head><body>
<h1>WiFi credentials saved!</h1>
<p>The device will now try to connect. This AP will close shortly.</p>
</body></html>
)html";

WifiProvisioning::WifiProvisioning() : connected_(false), http_server_(nullptr) {
}

WifiProvisioning::~WifiProvisioning() {
    StopHttpServer();
}

void WifiProvisioning::WifiEventHandler(void* arg, esp_event_base_t event_base,
                                         int32_t event_id, void* event_data) {
    auto* self = static_cast<WifiProvisioning*>(arg);

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                self->connected_ = false;
                ESP_LOGW(TAG, "WiFi disconnected");
                if (self->callback_) {
                    self->callback_(WifiProvState::DISCONNECTED, "");
                }
                // Auto-retry
                esp_wifi_connect();
                break;
            case WIFI_EVENT_AP_STACONNECTED: {
                auto* event = (wifi_event_ap_staconnected_t*)event_data;
                ESP_LOGI(TAG, "Station connected to AP, AID=%d", event->aid);
                break;
            }
            default:
                break;
        }
    }
}

void WifiProvisioning::IpEventHandler(void* arg, esp_event_base_t event_base,
                                       int32_t event_id, void* event_data) {
    auto* self = static_cast<WifiProvisioning*>(arg);

    if (event_id == IP_EVENT_STA_GOT_IP) {
        auto* event = (ip_event_got_ip_t*)event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        self->ip_address_ = ip_str;
        self->connected_ = true;
        ESP_LOGI(TAG, "Got IP: %s", ip_str);
        if (self->callback_) {
            self->callback_(WifiProvState::CONNECTED, self->ip_address_);
        }
    }
}

void WifiProvisioning::Init() {
    ESP_LOGI(TAG, "Initializing WiFi");

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default netifs
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, WifiEventHandler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, IpEventHandler, this, nullptr));
}

bool WifiProvisioning::HasStoredCredentials() {
    Settings settings(NVS_NAMESPACE_WIFI);
    std::string ssid = settings.GetString("ssid");
    return !ssid.empty();
}

void WifiProvisioning::ConnectStored() {
    Settings settings(NVS_NAMESPACE_WIFI);
    std::string ssid = settings.GetString("ssid");
    std::string password = settings.GetString("password");

    if (ssid.empty()) {
        ESP_LOGW(TAG, "No stored WiFi credentials");
        return;
    }

    ESP_LOGI(TAG, "Connecting to stored SSID: %s", ssid.c_str());
    ssid_ = ssid;

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (callback_) {
        callback_(WifiProvState::CONNECTING, ssid);
    }
}

void WifiProvisioning::StartAP(const std::string& ssid, const std::string& password) {
    ESP_LOGI(TAG, "Starting AP: %s", ssid.c_str());

    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, ssid.c_str(), sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = ssid.length();
    ap_config.ap.channel = WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = WIFI_AP_MAX_CONN;

    if (password.empty()) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strncpy((char*)ap_config.ap.password, password.c_str(), sizeof(ap_config.ap.password) - 1);
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    StartHttpServer();

    if (callback_) {
        callback_(WifiProvState::AP_ACTIVE, ssid);
    }
}

void WifiProvisioning::StopAP() {
    StopHttpServer();
    esp_wifi_stop();
}

bool WifiProvisioning::IsConnected() const {
    return connected_;
}

std::string WifiProvisioning::GetIpAddress() const {
    return ip_address_;
}

std::string WifiProvisioning::GetSsid() const {
    return ssid_;
}

void WifiProvisioning::StoreCredentials(const std::string& ssid, const std::string& password) {
    Settings settings(NVS_NAMESPACE_WIFI, true);
    settings.SetString("ssid", ssid);
    settings.SetString("password", password);
    ESP_LOGI(TAG, "WiFi credentials stored for SSID: %s", ssid.c_str());
}

// -- HTTP Server for captive portal --

static esp_err_t wifi_config_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WIFI_CONFIG_HTML, strlen(WIFI_CONFIG_HTML));
    return ESP_OK;
}

static esp_err_t wifi_config_post_handler(httpd_req_t* req) {
    auto* self = static_cast<WifiProvisioning*>(req->user_ctx);

    char buf[256] = {};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    // Parse form data: ssid=xxx&password=yyy
    std::string ssid, password;
    char* token = strtok(buf, "&");
    while (token) {
        if (strncmp(token, "ssid=", 5) == 0) {
            ssid = token + 5;
        } else if (strncmp(token, "password=", 9) == 0) {
            password = token + 9;
        }
        token = strtok(nullptr, "&");
    }

    // Basic URL decode (spaces)
    auto url_decode = [](std::string& s) {
        size_t pos = 0;
        while ((pos = s.find('+', pos)) != std::string::npos) {
            s.replace(pos, 1, " ");
        }
        pos = 0;
        while ((pos = s.find('%', pos)) != std::string::npos) {
            if (pos + 2 < s.length()) {
                char hex[3] = {s[pos + 1], s[pos + 2], '\0'};
                char ch = (char)strtol(hex, nullptr, 16);
                s.replace(pos, 3, 1, ch);
            }
            pos++;
        }
    };
    url_decode(ssid);
    url_decode(password);

    if (ssid.empty() || ssid.length() > 32) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID");
        return ESP_FAIL;
    }

    if (password.length() > 64) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password too long");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received WiFi config: SSID=%s", ssid.c_str());
    self->StoreCredentials(ssid, password);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WIFI_OK_HTML, strlen(WIFI_OK_HTML));

    // Schedule reconnection after a short delay
    // The main application loop will handle this
    if (self) {
        // Signal that we should try connecting
        // Done via state callback
    }

    return ESP_OK;
}

// Captive portal redirect: any unknown URL -> config page
static esp_err_t captive_portal_handler(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

void WifiProvisioning::StartHttpServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    httpd_uri_t get_uri = {};
    get_uri.uri = "/";
    get_uri.method = HTTP_GET;
    get_uri.handler = wifi_config_get_handler;
    get_uri.user_ctx = this;
    httpd_register_uri_handler(server, &get_uri);

    httpd_uri_t post_uri = {};
    post_uri.uri = "/wifi";
    post_uri.method = HTTP_POST;
    post_uri.handler = wifi_config_post_handler;
    post_uri.user_ctx = this;
    httpd_register_uri_handler(server, &post_uri);

    // Captive portal catch-all (must be registered last)
    httpd_uri_t catchall = {};
    catchall.uri = "/*";
    catchall.method = HTTP_GET;
    catchall.handler = captive_portal_handler;
    catchall.user_ctx = this;
    httpd_register_uri_handler(server, &catchall);

    http_server_ = server;
    ESP_LOGI(TAG, "HTTP config server started");
}

void WifiProvisioning::StopHttpServer() {
    if (http_server_) {
        httpd_stop((httpd_handle_t)http_server_);
        http_server_ = nullptr;
    }
}
