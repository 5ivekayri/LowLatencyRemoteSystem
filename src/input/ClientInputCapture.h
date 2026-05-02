#pragma once

#include "common/Result.h"

namespace remote::input {

class ClientInputCapture {
public:
    Result Start();
    void Stop();
};

} // namespace remote::input

