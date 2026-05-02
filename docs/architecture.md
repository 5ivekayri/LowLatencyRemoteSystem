# Architecture

## Goal

Build a Windows-first low-latency remote desktop / game streaming MVP using C++20, CMake, D3D11, DXGI Desktop Duplication, Media Foundation, Winsock2, and Win32 input APIs.

The system is for legitimate remote access only. The host is visible to the local user and sessions require explicit start plus a pairing/session token.

## Processes

- `remote_host`: captures the Windows desktop, encodes H.264 frames, sends video over UDP, accepts control/input messages, injects approved input with `SendInput`.
- `remote_client`: connects to a host, authenticates with a pairing token, receives UDP video, decodes H.264, renders with D3D11, captures local input.

## Host Pipeline

```text
DXGI Desktop Duplication
  -> D3D11 texture
  -> optional BGRA/NV12 conversion
  -> Media Foundation H.264 encoder
  -> EncodedFrame
  -> UDP packetization
  -> Client
```

Runtime rules:

- Keep capture-to-encode queue at 1-2 frames.
- Keep encode-to-network queue at 2-4 frames.
- Drop stale frames rather than increasing latency.
- Insert periodic keyframes and honor client `RequestKeyFrame`.

## Client Pipeline

```text
UDP receive
  -> packet reorder/reassembly
  -> encoded H.264 frame
  -> Media Foundation H.264 decoder
  -> D3D11 texture/frame
  -> D3D11 renderer
```

Runtime rules:

- Reassemble only recent frames.
- Drop incomplete frames after timeout.
- Request a keyframe after packet loss or decoder resync.
- Present the freshest complete decoded frame.

## Input Pipeline

```text
Client keyboard/mouse capture
  -> TCP control message in MVP
  -> Host session/token validation
  -> SendInput injection
```

Input has priority over video. Mouse move messages may be coalesced. Key up/down events must not be dropped.

## Windows Subsystems

- Capture: DXGI Desktop Duplication API.
- Graphics: D3D11 device/context.
- Codec: Media Foundation H.264 encoder/decoder.
- Transport: Winsock2 TCP control channel and UDP video channel.
- Window/input: Win32 API and `SendInput`.

## MVP Boundaries

The first skeleton compiles and validates subsystem initialization paths, but full capture, encode, decode, and renderer loops are milestone work. TODO comments must explain exact missing Windows-specific behavior.

