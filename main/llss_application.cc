#include "llss_application.h"

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <cstdio>

#include "llss_config.h"
#include "settings.h"

static const char* TAG = "LlssApp";

LlssApplication& LlssApplication::GetInstance() {
    static LlssApplication instance;
    return instance;
}

LlssApplication::LlssApplication()
    : i2c_bus_(nullptr), pmic_(nullptr), epd_(nullptr), input_(nullptr),
      client_(nullptr), event_group_(nullptr),
      poll_interval_ms_(LLSS_DEFAULT_POLL_MS),
      has_pending_button_(false) {
}

std::string LlssApplication::GenerateHardwareId() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

void LlssApplication::InitI2c() {
    ESP_LOGI(TAG, "Initializing I2C bus");
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = (i2c_port_t)0;
    bus_cfg.sda_io_num = PMIC_I2C_SDA_PIN;
    bus_cfg.scl_io_num = PMIC_I2C_SCL_PIN;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.intr_priority = 0;
    bus_cfg.trans_queue_depth = 0;
    bus_cfg.flags.enable_internal_pullup = 1;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &i2c_bus_));
}

void LlssApplication::InitPmic() {
    ESP_LOGI(TAG, "Initializing PMIC");
    pmic_ = new PmicAxp2101(i2c_bus_, PMIC_I2C_ADDR);
}

void LlssApplication::InitEpd() {
    ESP_LOGI(TAG, "Initializing EPD");
    EpdSpiConfig spi_cfg = {};
    spi_cfg.cs = EPD_CS_PIN;
    spi_cfg.dc = EPD_DC_PIN;
    spi_cfg.rst = EPD_RST_PIN;
    spi_cfg.busy = EPD_BUSY_PIN;
    spi_cfg.mosi = EPD_MOSI_PIN;
    spi_cfg.scl = EPD_SCK_PIN;
    spi_cfg.spi_host = EPD_SPI_NUM;

    epd_ = new EpdDriver(EPD_WIDTH, EPD_HEIGHT, spi_cfg);
    epd_->Init();
    epd_->Clear();
}

void LlssApplication::InitInput() {
    ESP_LOGI(TAG, "Initializing input manager");
    input_ = new InputManager(i2c_bus_, MCP23017_I2C_ADDR, MCP23017_INT_PIN,
                               ENTER_BUTTON_GPIO, ESC_BUTTON_GPIO,
                               HL_LEFT_BUTTON_GPIO, HL_RIGHT_BUTTON_GPIO);
    input_->Init();
    input_->SetEventCallback([this](const ButtonEvent& event) {
        ESP_LOGI(TAG, "Button: %s %s",
                 InputManager::ButtonIdToString(event.button),
                 InputManager::EventTypeToString(event.type));
        pending_button_event_ = event;
        has_pending_button_ = true;
        xEventGroupSetBits(event_group_, EVT_BUTTON_PRESS);
    });
}

void LlssApplication::InitWifi() {
    ESP_LOGI(TAG, "Initializing WiFi");
    wifi_.Init();
    wifi_.SetCallback([this](WifiProvState state, const std::string& info) {
        switch (state) {
            case WifiProvState::CONNECTED:
                ESP_LOGI(TAG, "WiFi connected: %s", info.c_str());
                xEventGroupSetBits(event_group_, EVT_WIFI_CONNECTED);
                break;
            case WifiProvState::DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected");
                xEventGroupSetBits(event_group_, EVT_WIFI_DISCONNECTED);
                break;
            case WifiProvState::AP_ACTIVE:
                ESP_LOGI(TAG, "AP active: %s", info.c_str());
                break;
            default:
                break;
        }
    });
}

void LlssApplication::ShowStatusMessage(const char* line1, const char* line2) {
    // For now, just log - in a full implementation, we'd render text to the EPD
    // using a simple bitmap font. The server will provide full frames once connected.
    ESP_LOGI(TAG, "STATUS: %s%s%s", line1, line2 ? " | " : "", line2 ? line2 : "");

    // Clear the EPD buffer with white
    int ps = epd_->plane_size();
    uint8_t* buf = (uint8_t*)heap_caps_malloc(ps, MALLOC_CAP_SPIRAM);
    if (!buf) return;
    memset(buf, 0xFF, ps);

    // Simple: draw a small indicator pattern in top-left to show device is alive
    // A 16-pixel wide black bar at top
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < EPD_WIDTH / 8; x++) {
            buf[y * (EPD_WIDTH / 8) + x] = 0x00;  // Black line
        }
    }

    epd_->DisplayFull(buf, ps);
    heap_caps_free(buf);
}

bool LlssApplication::LoadCredentials(DeviceCredentials& creds) {
    Settings settings(NVS_NAMESPACE_DEVICE);
    creds.device_id = settings.GetString("device_id");
    creds.device_secret = settings.GetString("device_secret");
    creds.refresh_token = settings.GetString("refresh_token");
    creds.access_token = settings.GetString("access_token");
    return !creds.device_id.empty();
}

void LlssApplication::SaveCredentials(const DeviceCredentials& creds) {
    Settings settings(NVS_NAMESPACE_DEVICE, true);
    settings.SetString("device_id", creds.device_id);
    settings.SetString("device_secret", creds.device_secret);
    settings.SetString("refresh_token", creds.refresh_token);
    settings.SetString("access_token", creds.access_token);
}

void LlssApplication::Initialize() {
    ESP_LOGI(TAG, "=== LLSS e-Ink Client v%s ===", LLSS_FIRMWARE_VERSION);

    event_group_ = xEventGroupCreate();
    hardware_id_ = GenerateHardwareId();
    ESP_LOGI(TAG, "Hardware ID: %s", hardware_id_.c_str());

    // Initialize hardware
    InitI2c();
    InitPmic();
    InitEpd();
    InitInput();
    InitWifi();

    // Initialize LLSS client
    client_ = new LlssClient(LLSS_SERVER_URL);

    ESP_LOGI(TAG, "Initialization complete");
}

void LlssApplication::HandleBoot() {
    ShowStatusMessage("Booting...");

    if (wifi_.HasStoredCredentials()) {
        state_machine_.SetState(DeviceState::WIFI_CONNECTING);
        wifi_.ConnectStored();
    } else {
        state_machine_.SetState(DeviceState::WIFI_CONFIG);
        HandleWifiConfig();
    }
}

void LlssApplication::HandleWifiConfig() {
    ShowStatusMessage("WiFi Setup", "Connect to LLSS-xxxx AP");

    std::string ap_ssid = std::string(WIFI_AP_SSID_PREFIX) + hardware_id_.substr(8);
    wifi_.StartAP(ap_ssid);

    // Wait for WiFi credentials to be submitted and connection established
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(event_group_,
            EVT_WIFI_CONNECTED | EVT_WIFI_AP_DONE | EVT_SHUTDOWN,
            pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));

        // Poll buttons even during WiFi config
        input_->Poll();

        if (bits & EVT_WIFI_CONNECTED) {
            wifi_.StopAP();
            state_machine_.SetState(DeviceState::WIFI_CONNECTING);
            state_machine_.SetState(DeviceState::WIFI_CONNECTED);
            return;
        }

        if (bits & EVT_SHUTDOWN) {
            pmic_->PowerOff();
            return;
        }

        // Check if new credentials were stored (user submitted form)
        if (wifi_.HasStoredCredentials() && !wifi_.IsConnected()) {
            wifi_.StopAP();
            state_machine_.SetState(DeviceState::WIFI_CONNECTING);
            wifi_.ConnectStored();

            // Wait for connection result
            EventBits_t conn_bits = xEventGroupWaitBits(event_group_,
                EVT_WIFI_CONNECTED | EVT_WIFI_DISCONNECTED,
                pdTRUE, pdFALSE, pdMS_TO_TICKS(15000));

            if (conn_bits & EVT_WIFI_CONNECTED) {
                state_machine_.SetState(DeviceState::WIFI_CONNECTED);
                return;
            }

            // Connection failed, restart AP
            state_machine_.SetState(DeviceState::WIFI_CONFIG);
            wifi_.StartAP(ap_ssid);
        }
    }
}

void LlssApplication::HandleWifiConnected() {
    DeviceCredentials creds;
    if (LoadCredentials(creds)) {
        ESP_LOGI(TAG, "Loaded device credentials: %s", creds.device_id.c_str());
        client_->SetCredentials(creds);

        if (!creds.refresh_token.empty()) {
            // Have refresh token — go straight to authentication (get access token)
            state_machine_.SetState(DeviceState::AUTHENTICATING);
        } else if (!creds.device_secret.empty()) {
            // Have device_secret but no refresh token — need to authenticate
            state_machine_.SetState(DeviceState::AUTHENTICATING);
        } else {
            // Only device_id, no secret — re-register
            state_machine_.SetState(DeviceState::REGISTERING);
        }
    } else {
        state_machine_.SetState(DeviceState::REGISTERING);
    }
}

void LlssApplication::HandleRegistration() {
    ShowStatusMessage("Registering device...");

    DeviceCredentials creds;
    bool ok = client_->RegisterDevice(
        hardware_id_, LLSS_FIRMWARE_VERSION,
        EPD_WIDTH, EPD_HEIGHT, EPD_BIT_DEPTH, true,
        creds);

    if (ok) {
        SaveCredentials(creds);
        client_->SetCredentials(creds);
        ESP_LOGI(TAG, "Registered, pending admin authorization");
        state_machine_.SetState(DeviceState::WAITING_AUTHORIZATION);
    } else {
        // 409 = already exists, try to authenticate with stored creds
        DeviceCredentials stored;
        if (LoadCredentials(stored) && !stored.device_secret.empty()) {
            client_->SetCredentials(stored);
            state_machine_.SetState(DeviceState::AUTHENTICATING);
        } else {
            ESP_LOGE(TAG, "Registration failed, retrying in 10s");
            vTaskDelay(pdMS_TO_TICKS(10000));
            // Stay in REGISTERING, will retry
        }
    }
}

void LlssApplication::HandleWaitingAuthorization() {
    ShowStatusMessage("Waiting for admin", "authorization...");

    // Poll /auth/devices/token periodically to check if authorized
    DeviceCredentials creds = client_->GetCredentials();

    AuthStatus status = client_->AuthenticateDevice(
        hardware_id_, creds.device_secret,
        LLSS_FIRMWARE_VERSION,
        EPD_WIDTH, EPD_HEIGHT, EPD_BIT_DEPTH, true,
        creds);

    switch (status) {
        case AuthStatus::AUTHORIZED:
            ESP_LOGI(TAG, "Device authorized! Got refresh token.");
            SaveCredentials(creds);
            client_->SetCredentials(creds);
            state_machine_.SetState(DeviceState::AUTHENTICATING);
            break;

        case AuthStatus::PENDING:
            ESP_LOGI(TAG, "Still pending authorization, retry in 15s");
            vTaskDelay(pdMS_TO_TICKS(15000));
            // Stay in WAITING_AUTHORIZATION
            break;

        case AuthStatus::REJECTED:
        case AuthStatus::REVOKED:
            ESP_LOGE(TAG, "Device rejected/revoked");
            ShowStatusMessage("Device rejected", "Contact admin");
            state_machine_.SetState(DeviceState::ERROR);
            break;

        default:
            ESP_LOGE(TAG, "Auth check failed, retry in 10s");
            vTaskDelay(pdMS_TO_TICKS(10000));
            break;
    }
}

void LlssApplication::HandleAuthentication() {
    DeviceCredentials creds = client_->GetCredentials();

    // If we have a refresh token, exchange it for an access token
    if (!creds.refresh_token.empty()) {
        std::string access_token;
        if (client_->RefreshAccessToken(creds.refresh_token, access_token)) {
            creds.access_token = access_token;
            SaveCredentials(creds);
            client_->SetCredentials(creds);
            ESP_LOGI(TAG, "Got access token, ready to poll");
            state_machine_.SetState(DeviceState::POLLING);
            return;
        }

        // Refresh token expired/invalid — try to get a new one with device_secret
        ESP_LOGW(TAG, "Refresh token invalid, re-authenticating");
        creds.refresh_token.clear();
    }

    // No refresh token — authenticate with device_secret to get one
    if (!creds.device_secret.empty()) {
        AuthStatus status = client_->AuthenticateDevice(
            hardware_id_, creds.device_secret,
            LLSS_FIRMWARE_VERSION,
            EPD_WIDTH, EPD_HEIGHT, EPD_BIT_DEPTH, true,
            creds);

        switch (status) {
            case AuthStatus::AUTHORIZED:
                if (!creds.refresh_token.empty()) {
                    SaveCredentials(creds);
                    client_->SetCredentials(creds);
                    // Now get access token from refresh token
                    std::string access_token;
                    if (client_->RefreshAccessToken(creds.refresh_token, access_token)) {
                        creds.access_token = access_token;
                        SaveCredentials(creds);
                        state_machine_.SetState(DeviceState::POLLING);
                    } else {
                        ESP_LOGE(TAG, "Failed to get access token, retry in 5s");
                        vTaskDelay(pdMS_TO_TICKS(5000));
                    }
                }
                break;

            case AuthStatus::PENDING:
                state_machine_.SetState(DeviceState::WAITING_AUTHORIZATION);
                break;

            case AuthStatus::REJECTED:
            case AuthStatus::REVOKED:
                ShowStatusMessage("Device rejected", "Contact admin");
                state_machine_.SetState(DeviceState::ERROR);
                break;

            default:
                ESP_LOGE(TAG, "Authentication failed, retry in 10s");
                vTaskDelay(pdMS_TO_TICKS(10000));
                break;
        }
    } else {
        // No credentials at all — need to register
        state_machine_.SetState(DeviceState::REGISTERING);
    }
}

bool LlssApplication::EnsureAccessToken() {
    // If last call got 401, refresh the access token
    if (!client_->LastCallWas401()) return true;

    ESP_LOGW(TAG, "Got 401, refreshing access token");
    DeviceCredentials creds = client_->GetCredentials();
    if (creds.refresh_token.empty()) {
        state_machine_.SetState(DeviceState::AUTHENTICATING);
        return false;
    }

    std::string access_token;
    if (client_->RefreshAccessToken(creds.refresh_token, access_token)) {
        creds.access_token = access_token;
        SaveCredentials(creds);
        return true;
    }

    // Refresh token also failed — full re-auth
    state_machine_.SetState(DeviceState::AUTHENTICATING);
    return false;
}

void LlssApplication::HandlePolling() {
    DeviceStateResponse response;
    bool ok = client_->GetDeviceState(last_frame_id_, last_event_id_, response);

    if (!ok) {
        if (client_->LastCallWas401()) {
            if (!EnsureAccessToken()) return;
            // Retry after token refresh
            ok = client_->GetDeviceState(last_frame_id_, last_event_id_, response);
            if (!ok) {
                ESP_LOGE(TAG, "Poll failed after token refresh");
                poll_interval_ms_ = LLSS_MAX_POLL_MS;
                return;
            }
        } else {
            ESP_LOGE(TAG, "Poll failed, will retry");
            poll_interval_ms_ = LLSS_MAX_POLL_MS;
            return;
        }
    }

    active_instance_id_ = response.active_instance_id;
    poll_interval_ms_ = response.poll_after_ms;

    // Clamp poll interval
    if (poll_interval_ms_ < LLSS_MIN_POLL_MS) poll_interval_ms_ = LLSS_MIN_POLL_MS;
    if (poll_interval_ms_ > LLSS_MAX_POLL_MS) poll_interval_ms_ = LLSS_MAX_POLL_MS;

    switch (response.action) {
        case LlssAction::FETCH_FRAME:
            state_machine_.SetState(DeviceState::FETCHING_FRAME);
            HandleFetchFrame(response.frame_id);
            break;

        case LlssAction::SLEEP:
            state_machine_.SetState(DeviceState::SLEEPING);
            HandleSleep(poll_interval_ms_);
            break;

        case LlssAction::NOOP:
        default:
            // Just wait and poll again
            break;
    }
}

void LlssApplication::HandleFetchFrame(const std::string& frame_id) {
    ESP_LOGI(TAG, "Fetching frame: %s", frame_id.c_str());

    int frame_len = 0;
    uint8_t* framebuffer = client_->FetchFrame(frame_id, frame_len);

    if (!framebuffer || frame_len == 0) {
        ESP_LOGE(TAG, "Failed to fetch frame");
        state_machine_.SetState(DeviceState::POLLING);
        return;
    }

    int ps = epd_->plane_size();
    int gs_size = epd_->grayscale_buffer_size();
    bool is_grayscale = (frame_len == gs_size);
    bool is_mono = (frame_len == ps);

    if (!is_grayscale && !is_mono) {
        ESP_LOGW(TAG, "Frame size %d doesn't match mono (%d) or grayscale (%d)", frame_len, ps, gs_size);
        // Treat as mono if at least one plane's worth of data
        if (frame_len >= ps) {
            is_mono = true;
        } else {
            // Pad with white
            uint8_t* padded = (uint8_t*)heap_caps_realloc(framebuffer, ps,
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (padded) {
                memset(padded + frame_len, 0xFF, ps - frame_len);
                framebuffer = padded;
                frame_len = ps;
            }
            is_mono = true;
        }
    }

    state_machine_.SetState(DeviceState::DISPLAYING);

    if (last_frame_id_.empty()) {
        // First frame - full refresh
        if (is_grayscale) {
            epd_->DisplayGrayscaleFull(framebuffer, gs_size);
        } else {
            epd_->DisplayFull(framebuffer, ps);
        }
    } else {
        // Subsequent frames - partial refresh
        epd_->InitPartial();
        if (is_grayscale) {
            epd_->DisplayGrayscalePartial(framebuffer, gs_size);
        } else {
            epd_->DisplayPartial(framebuffer, ps);
        }
    }

    last_frame_id_ = frame_id;
    heap_caps_free(framebuffer);

    state_machine_.SetState(DeviceState::POLLING);
}

void LlssApplication::HandleSendInput(const ButtonEvent& event) {
    state_machine_.SetState(DeviceState::SENDING_INPUT);

    const char* btn_name = InputManager::ButtonIdToString(event.button);
    const char* evt_type = InputManager::EventTypeToString(event.type);

    InputProcessResult result;
    bool ok = client_->SendInput(btn_name, evt_type, result);

    if (!ok) {
        if (client_->LastCallWas401()) {
            if (EnsureAccessToken()) {
                ok = client_->SendInput(btn_name, evt_type, result);
            }
        }
        if (!ok) {
            ESP_LOGE(TAG, "Failed to send input event");
            state_machine_.SetState(DeviceState::POLLING);
            return;
        }
    }

    switch (result.status) {
        case InputResultStatus::NEW_FRAME:
            if (!result.frame_id.empty()) {
                state_machine_.SetState(DeviceState::FETCHING_FRAME);
                HandleFetchFrame(result.frame_id);
                return;
            }
            break;

        case InputResultStatus::POLL:
            if (result.poll_after_ms > 0) {
                poll_interval_ms_ = result.poll_after_ms;
            }
            break;

        default:
            break;
    }

    state_machine_.SetState(DeviceState::POLLING);
}

void LlssApplication::HandleSleep(int duration_ms) {
    ESP_LOGI(TAG, "Sleeping for %d ms", duration_ms);
    epd_->Sleep();
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    epd_->Init();
    state_machine_.SetState(DeviceState::POLLING);
}

void LlssApplication::Run() {
    ESP_LOGI(TAG, "Starting main event loop");

    // Start with boot sequence
    state_machine_.SetState(DeviceState::BOOTING);
    HandleBoot();

    // If we exited boot, we should be in WIFI_CONNECTED
    if (state_machine_.GetState() == DeviceState::WIFI_CONNECTED) {
        HandleWifiConnected();
    }

    // Main event loop
    while (true) {
        DeviceState current = state_machine_.GetState();

        // Handle registration flow
        if (current == DeviceState::REGISTERING) {
            HandleRegistration();
            continue;
        }

        if (current == DeviceState::WAITING_AUTHORIZATION) {
            HandleWaitingAuthorization();
            continue;
        }

        if (current == DeviceState::AUTHENTICATING) {
            HandleAuthentication();
            continue;
        }

        // Main polling loop
        if (current == DeviceState::POLLING) {
            // Wait for either poll timer or button press
            EventBits_t bits = xEventGroupWaitBits(event_group_,
                EVT_BUTTON_PRESS | EVT_WIFI_DISCONNECTED | EVT_SHUTDOWN,
                pdTRUE, pdFALSE, pdMS_TO_TICKS(poll_interval_ms_));

            // Always poll input hardware
            input_->Poll();

            if (bits & EVT_SHUTDOWN) {
                epd_->Sleep();
                pmic_->PowerOff();
                vTaskDelay(portMAX_DELAY);
            }

            if (bits & EVT_WIFI_DISCONNECTED) {
                ESP_LOGW(TAG, "WiFi lost, waiting for reconnection");
                ShowStatusMessage("WiFi disconnected", "Reconnecting...");

                // Wait for reconnection
                EventBits_t conn_bits = xEventGroupWaitBits(event_group_,
                    EVT_WIFI_CONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
                if (!(conn_bits & EVT_WIFI_CONNECTED)) {
                    // Restart WiFi config
                    state_machine_.SetState(DeviceState::ERROR);
                    state_machine_.SetState(DeviceState::BOOTING);
                    HandleBoot();
                }
                continue;
            }

            if (bits & EVT_BUTTON_PRESS) {
                if (has_pending_button_) {
                    has_pending_button_ = false;
                    HandleSendInput(pending_button_event_);
                    // After input, poll immediately if still in POLLING
                    if (state_machine_.GetState() == DeviceState::POLLING) {
                        HandlePolling();
                    }
                    continue;
                }
            }

            // Regular poll (check state in case auth handler changed it)
            if (state_machine_.GetState() == DeviceState::POLLING) {
                HandlePolling();
            }
            continue;
        }

        if (current == DeviceState::ERROR) {
            ESP_LOGE(TAG, "In ERROR state, waiting 30s before retry");
            vTaskDelay(pdMS_TO_TICKS(30000));
            state_machine_.SetState(DeviceState::BOOTING);
            HandleBoot();
            if (state_machine_.GetState() == DeviceState::WIFI_CONNECTED) {
                HandleWifiConnected();
            }
            continue;
        }

        // Fallback: small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(100));
        input_->Poll();
    }
}
