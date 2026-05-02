#pragma once

#include "common/Result.h"

namespace remote::capture {

class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;
    virtual Result Initialize() = 0;
    virtual Result CaptureNextFrameStub() = 0;
};

} // namespace remote::capture

