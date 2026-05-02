#pragma once

#include "common/Result.h"

namespace remote::transport {

class ITransport {
public:
    virtual ~ITransport() = default;
    virtual Result Start() = 0;
    virtual void Stop() = 0;
};

} // namespace remote::transport

