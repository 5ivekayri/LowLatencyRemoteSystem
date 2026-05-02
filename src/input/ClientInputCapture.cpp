#include "input/ClientInputCapture.h"

#include "common/Log.h"

namespace remote::input {

Result ClientInputCapture::Start() {
    // TODO: Install a visible client-window input path. Avoid global hooks for MVP unless the user explicitly opts in.
    Log(LogLevel::Info, "ClientInputCapture started (stub)");
    return Result::Ok();
}

void ClientInputCapture::Stop() {
    Log(LogLevel::Info, "ClientInputCapture stopped");
}

} // namespace remote::input

