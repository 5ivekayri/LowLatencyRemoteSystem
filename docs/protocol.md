# Protocol v0

Protocol v0 is intentionally small, binary-friendly, and LAN-oriented. It is not Internet-safe and is not a replacement for real authentication or encryption.

## Safety Requirements

- Every session requires an explicit pairing token.
- The host rejects control and input messages until a token is accepted.
- Session IDs are generated per process run and are not stable credentials.
- No unauthenticated remote control is allowed, even on LAN.

## TCP Control Channel

Default port: `47990`.

All TCP control messages use:

```text
MessageHeader {
  uint32 magic = "LLRS";
  uint16 version = 0;
  uint16 type;
  uint32 payloadSize;
}
payload[payloadSize]
```

All integer fields are little-endian for v0 because both MVP peers are Windows-first. A future version should define network byte order or a fixed endian conversion layer.

### Message Types

- `Hello`: client protocol version, client name, requested UDP port, pairing token hash/plain dev token for MVP.
- `HostInfo`: host name, desktop size, refresh estimate, codec support.
- `AuthToken`: token exchange/result for MVP pairing.
- `StartStream`: requested width, height, fps, bitrate, codec.
- `StopStream`: stop video streaming.
- `RequestKeyFrame`: client asks host to force next frame to IDR/keyframe.
- `SetBitrate`: client-side network adaptation request.
- `InputEvent`: keyboard/mouse event in MVP.
- `Stats`: latency, packet loss, queue depth, encode/decode/present timing.
- `Ping` / `Pong`: RTT measurement.

## UDP Video Channel

Default port: `47991`.

Target payload size: about `1150` bytes to avoid fragmentation on typical LAN MTU.

```text
PacketHeader {
  uint32 magic = "LLRV";
  uint16 version = 0;
  uint16 headerSize;
  uint64 sessionId;
  uint64 sequence;
  uint64 timestampUs;
  uint64 frameId;
  uint16 fragmentIndex;
  uint16 fragmentCount;
  uint16 flags;
  uint16 payloadSize;
}
payload[payloadSize]
```

### Flags

- `KeyFrame = 1 << 0`
- `EndOfFrame = 1 << 1`
- `ConfigPacket = 1 << 2`

## Drop Policy

- Host never retransmits video packets in v0.
- Client drops incomplete frames after timeout.
- Client may request keyframe after loss.
- Stale complete frames are dropped if a newer decoded frame is available.

