#include "input/InputInjector.h"

#include "common/Log.h"

#include <windows.h>

namespace remote::input {

Result InputInjector::Inject(const InputEvent& event) {
    switch (event.type) {
    case InputEventType::MouseMove:
        return InjectMouseMove(event);
    case InputEventType::MouseButton:
        return InjectMouseButton(event);
    case InputEventType::MouseWheel:
        return InjectMouseWheel(event);
    case InputEventType::Key:
        return InjectKey(event);
    }
    return Result::Fail("unknown input event type");
}

Result InputInjector::InjectMouseMove(const InputEvent& event) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = event.dx;
    input.mi.dy = event.dy;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;

    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        LogWin32Error("SendInput(mouse move)", GetLastError());
        return Result::Fail("SendInput mouse move failed");
    }
    return Result::Ok();
}

Result InputInjector::InjectMouseButton(const InputEvent& event) {
    DWORD flag = 0;
    switch (event.button) {
    case MouseButton::Left:
        flag = event.pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        break;
    case MouseButton::Right:
        flag = event.pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        break;
    case MouseButton::Middle:
        flag = event.pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        break;
    }

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flag;
    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        LogWin32Error("SendInput(mouse button)", GetLastError());
        return Result::Fail("SendInput mouse button failed");
    }
    return Result::Ok();
}

Result InputInjector::InjectMouseWheel(const InputEvent& event) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = static_cast<DWORD>(event.wheelDelta);
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        LogWin32Error("SendInput(mouse wheel)", GetLastError());
        return Result::Fail("SendInput mouse wheel failed");
    }
    return Result::Ok();
}

Result InputInjector::InjectKey(const InputEvent& event) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = event.virtualKey;
    input.ki.dwFlags = event.pressed ? 0 : KEYEVENTF_KEYUP;
    if (SendInput(1, &input, sizeof(INPUT)) != 1) {
        LogWin32Error("SendInput(key)", GetLastError());
        return Result::Fail("SendInput key failed");
    }
    return Result::Ok();
}

} // namespace remote::input

