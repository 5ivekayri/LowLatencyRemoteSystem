#pragma once

#include <cstdint>

namespace remote::input {

enum class InputEventType : uint8_t {
    MouseMove,
    MouseButton,
    MouseWheel,
    Key,
};

enum class MouseButton : uint8_t {
    Left,
    Right,
    Middle,
};

struct InputEvent {
    InputEventType type = InputEventType::MouseMove;
    uint64_t timestampUs = 0;
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t wheelDelta = 0;
    uint16_t virtualKey = 0;
    MouseButton button = MouseButton::Left;
    bool pressed = false;
};

} // namespace remote::input

