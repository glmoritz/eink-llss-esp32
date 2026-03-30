#ifndef LLSS_STATE_H
#define LLSS_STATE_H

#include <atomic>
#include <functional>

enum class DeviceState {
    BOOTING,
    WIFI_CONFIG,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    REGISTERING,
    POLLING,
    FETCHING_FRAME,
    DISPLAYING,
    SENDING_INPUT,
    SLEEPING,
    ERROR
};

class LlssStateMachine {
public:
    using StateChangeCallback = std::function<void(DeviceState old_state, DeviceState new_state)>;

    LlssStateMachine();

    DeviceState GetState() const { return state_.load(); }
    bool SetState(DeviceState new_state);

    void SetStateChangeCallback(StateChangeCallback callback) { callback_ = callback; }

    static const char* StateName(DeviceState state);

private:
    std::atomic<DeviceState> state_;
    StateChangeCallback callback_;

    bool IsValidTransition(DeviceState from, DeviceState to) const;
};

#endif // LLSS_STATE_H
