#pragma once

#include "common/ByteBuffer.h"
#include "protocol/Protocol.h"

namespace remote::net {

struct ReceivedPacket {
    protocol::PacketHeader header;
    ByteBuffer payload;
};

} // namespace remote::net

