#pragma once

namespace remote::net {

class JitterBuffer {
public:
    // TODO: Keep only a small window of recent frames and drop stale packets aggressively.
    void Clear() noexcept {}
};

} // namespace remote::net

