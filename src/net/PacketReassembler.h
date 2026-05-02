#pragma once

namespace remote::net {

class PacketReassembler {
public:
    // TODO: Reassemble fragments by frameId and evict incomplete frames after timeout.
    void Clear() noexcept {}
};

} // namespace remote::net

