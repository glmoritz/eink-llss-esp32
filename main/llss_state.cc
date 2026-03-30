#include "llss_state.h"

#include <esp_log.h>

static const char* TAG = "LlssState";

LlssStateMachine::LlssStateMachine() : state_(DeviceState::BOOTING) {
}

const char* LlssStateMachine::StateName(DeviceState state) {
    switch (state) {
        case DeviceState::BOOTING:         return "BOOTING";
        case DeviceState::WIFI_CONFIG:     return "WIFI_CONFIG";
        case DeviceState::WIFI_CONNECTING: return "WIFI_CONNECTING";
        case DeviceState::WIFI_CONNECTED:  return "WIFI_CONNECTED";
        case DeviceState::REGISTERING:     return "REGISTERING";
        case DeviceState::POLLING:         return "POLLING";
        case DeviceState::FETCHING_FRAME:  return "FETCHING_FRAME";
        case DeviceState::DISPLAYING:      return "DISPLAYING";
        case DeviceState::SENDING_INPUT:   return "SENDING_INPUT";
        case DeviceState::SLEEPING:        return "SLEEPING";
        case DeviceState::ERROR:           return "ERROR";
        default:                           return "UNKNOWN";
    }
}

bool LlssStateMachine::IsValidTransition(DeviceState from, DeviceState to) const {
    // Allow transitions to ERROR from any state
    if (to == DeviceState::ERROR) return true;

    // Allow BOOTING to restart from ERROR
    if (from == DeviceState::ERROR && to == DeviceState::BOOTING) return true;

    switch (from) {
        case DeviceState::BOOTING:
            return to == DeviceState::WIFI_CONFIG ||
                   to == DeviceState::WIFI_CONNECTING;

        case DeviceState::WIFI_CONFIG:
            return to == DeviceState::WIFI_CONNECTING ||
                   to == DeviceState::BOOTING;

        case DeviceState::WIFI_CONNECTING:
            return to == DeviceState::WIFI_CONNECTED ||
                   to == DeviceState::WIFI_CONFIG;

        case DeviceState::WIFI_CONNECTED:
            return to == DeviceState::REGISTERING ||
                   to == DeviceState::POLLING;

        case DeviceState::REGISTERING:
            return to == DeviceState::POLLING;

        case DeviceState::POLLING:
            return to == DeviceState::FETCHING_FRAME ||
                   to == DeviceState::SENDING_INPUT ||
                   to == DeviceState::SLEEPING ||
                   to == DeviceState::WIFI_CONNECTING;

        case DeviceState::FETCHING_FRAME:
            return to == DeviceState::DISPLAYING ||
                   to == DeviceState::POLLING;

        case DeviceState::DISPLAYING:
            return to == DeviceState::POLLING;

        case DeviceState::SENDING_INPUT:
            return to == DeviceState::POLLING ||
                   to == DeviceState::FETCHING_FRAME;

        case DeviceState::SLEEPING:
            return to == DeviceState::POLLING ||
                   to == DeviceState::BOOTING;

        default:
            return false;
    }
}

bool LlssStateMachine::SetState(DeviceState new_state) {
    DeviceState old_state = state_.load();

    if (old_state == new_state) {
        return true;
    }

    if (!IsValidTransition(old_state, new_state)) {
        ESP_LOGW(TAG, "Invalid state transition: %s -> %s",
                 StateName(old_state), StateName(new_state));
        return false;
    }

    state_.store(new_state);
    ESP_LOGI(TAG, "State: %s -> %s", StateName(old_state), StateName(new_state));

    if (callback_) {
        callback_(old_state, new_state);
    }
    return true;
}
