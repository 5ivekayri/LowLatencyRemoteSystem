# Roadmap

## Milestone 1: Project Skeleton and Docs

- CMake project with `remote_host` and `remote_client`.
- Safety rules in `AGENTS.md`.
- Architecture, protocol, latency budget, and build instructions.
- Common logging/result/clock utilities.

## Milestone 2: Networking Skeleton

- Winsock startup RAII.
- TCP control channel accept/connect/send/receive framing.
- UDP video transport bind/send/receive packet shell.
- Protocol structs and serializer helpers.
- Token-required handshake path.

## Milestone 3: D3D11 and DXGI Capture Skeleton

- D3D11 device creation.
- DXGI output duplication initialization.
- Stub capture loop returning precise TODO until frame acquisition is wired.

## Milestone 4: Media Foundation Encoder Skeleton

- Media Foundation startup/shutdown RAII.
- H.264 encoder config structure.
- Encoder interface with low-latency TODOs: no B-frames, GOP 1-2 seconds, 1080p60, 15-30 Mbps.

## Milestone 5: Client Decoder and Renderer Skeleton

- H.264 decoder interface.
- D3D11 renderer window/device path.
- Present timing telemetry hooks.

## Milestone 6: Input Skeleton

- Client keyboard/mouse event model.
- TCP `InputEvent` messages.
- Host `SendInput` injection with session validation.

## Milestone 7: End-to-End LAN Prototype

- Capture one desktop output.
- Encode frames with Media Foundation.
- Packetize/reassemble UDP frames.
- Decode and render.
- Log capture/encode/network/decode/present latencies.

