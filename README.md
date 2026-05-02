# LowLatencyRemoteSystem

Windows-first C++20 MVP skeleton for a legitimate, explicit-consent remote desktop / game streaming system.

## Build

Preferred path for this repository is the checked-in Visual Studio solution:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" .\LowLatencyRemoteSystem.slnx /p:Configuration=Debug /p:Platform=x64 /m
```

Alternative CMake path, if CMake and a VS 2022 toolset are installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

Outputs:

- `remote_host`
- `remote_client`

## Run

MVP skeleton commands:

```powershell
.\build\Debug\remote_host.exe --token dev-token --tcp-port 48000 --udp-port 47991
.\build\Debug\remote_client.exe --host 127.0.0.1 --token dev-token --tcp-port 48000 --udp-port 47991
```

Visual Studio solution defaults use TCP control port `48000`:

```powershell
.\x64\Debug\remote_host.exe --mode dxgi --monitor 0 --fps 30 --token dev-token --tcp-port 48000 --udp-port 47991
.\x64\Debug\remote_client.exe --host 127.0.0.1 --token dev-token --tcp-port 48000 --udp-port 48001
```

Convenience launcher for two console windows. DXGI is the default for the capture milestone; pass `dummy` explicitly for the test pattern:

```powershell
.\x64\Debug\remote_launcher.exe
.\x64\Debug\remote_launcher.exe dxgi
.\x64\Debug\remote_launcher.exe dummy
.\run-local.ps1 -Mode dxgi -Monitor 0 -Fps 30
```

DXGI adapter/output discovery:

```powershell
.\x64\Debug\remote_host.exe --list-adapters
.\x64\Debug\remote_host.exe --mode dxgi --adapter 0 --output 0 --fps 30 --bind 127.0.0.1 --tcp 48000
```

See `docs/runbook.md` for LAN commands and firewall notes.

Debug GUI:

```powershell
.\x64\Debug\remote_debug_gui.exe
```

The GUI lets you select host mode, adapter/output, bind address, ports, token, and client host/bind settings, then starts/stops `remote_host.exe` and `remote_client.exe` in separate console windows.

The current implementation initializes app lifecycle, TCP handshake, UDP packetization/reassembly, and a raw BGRA D3D11 render path. Full H.264 encode/decode and real screen capture are intentionally milestone TODOs.

## Safety

This project is only for authorized remote access to devices you own or devices where the local user has explicitly approved the session. The host is visible and requires an explicit pairing/session token.
