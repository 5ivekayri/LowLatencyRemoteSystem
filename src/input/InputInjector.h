#pragma once

#include "common/Result.h"
#include "input/InputEvents.h"

namespace remote::input {

class InputInjector {
public:
    Result Inject(const InputEvent& event);

private:
    Result InjectMouseMove(const InputEvent& event);
    Result InjectMouseButton(const InputEvent& event);
    Result InjectMouseWheel(const InputEvent& event);
    Result InjectKey(const InputEvent& event);
};

} // namespace remote::input

