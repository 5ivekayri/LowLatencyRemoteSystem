# LowLatencyRemoteSystem Engineering Rules

This project is a Windows-first C++20 MVP for legitimate, user-approved remote desktop and game streaming.

## Safety and Product Rules

- Do not implement hidden access, persistence, stealth behavior, UAC bypass, or authorization bypass.
- Host must be visible to the local user and explicitly started for every MVP session.
- Remote control must require a pairing/session token, including LAN-only development builds.
- Code and documentation must frame the system as legal remote access to owned devices or devices used with explicit permission.
- Prefer clear failure and logging over silent fallback for security-sensitive decisions.

## Architecture Rules

- Every milestone must compile.
- Keep changes small and incremental; prefer stubs with precise TODOs over large incomplete implementations.
- Use C++20, CMake, MSVC, Windows SDK APIs, and no third-party dependencies for MVP.
- Use RAII for COM, Win32 handles, Winsock startup, and Media Foundation startup.
- Prefer `Microsoft::WRL::ComPtr` for COM pointers.
- Avoid global mutable state.
- Keep interfaces separate from Windows-specific implementations.

## Low-Latency Rules

- Prefer low latency over perfect reliability.
- Keep capture-to-encode queue size around 1-2 frames.
- Keep encode-to-network queue size around 2-4 frames.
- Drop stale or incomplete video frames after timeout.
- Do not build large frame queues.
- Input events have priority over video.
- Mouse movement may be coalesced; key up/down events must not be dropped.

## Error Handling and Logging

- Return `remote::Result` for fallible operations where practical.
- Log every failed Windows `HRESULT` or Win32/Winsock error with context and hex/error code.
- Include timestamps in logs when useful for latency analysis.
- Do not swallow protocol parse failures; reject the message and log the reason.

