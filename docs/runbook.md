# Runbook

## Local Dummy

Host:

```powershell
.\x64\Debug\remote_host.exe --mode dummy --bind 127.0.0.1 --tcp 48000
```

Client:

```powershell
.\x64\Debug\remote_client.exe --host 127.0.0.1 --tcp 48000 --udp 48001
```

## List DXGI Adapters

```powershell
.\x64\Debug\remote_host.exe --list-adapters
```

## Local DXGI

Host:

```powershell
.\x64\Debug\remote_host.exe --mode dxgi --adapter 0 --output 0 --width 640 --height 360 --fps 10 --max-mbps 150 --bind 127.0.0.1 --tcp 48000
```

Client:

```powershell
.\x64\Debug\remote_client.exe --host 127.0.0.1 --tcp 48000 --udp 48001
```

## LAN Test

On host PC:

```powershell
.\x64\Debug\remote_host.exe --mode dxgi --adapter 0 --output 0 --width 640 --height 360 --fps 10 --max-mbps 100 --bind 0.0.0.0 --tcp 48000
```

On client PC:

```powershell
.\x64\Debug\remote_client.exe --host HOST_LAN_IP --tcp 48000 --udp 48001 --bind 0.0.0.0
```

Windows Firewall:

- Allow `remote_host.exe` inbound TCP `48000`.
- Allow `remote_client.exe` inbound UDP `48001`.
- Prefer Private network rules during LAN testing.

Security:

- Default host bind is `127.0.0.1`.
- `--bind 0.0.0.0` enables LAN exposure and keeps pairing token required.
- Do not use a weak/default token outside a trusted local test network.

## Debugging Sequence

1. List adapters:

```powershell
.\x64\Debug\remote_host.exe --list-adapters
```

2. Test dummy frame:

```powershell
.\x64\Debug\remote_host.exe --self-test dummy
```

Expected:

- `artifacts\dummy_frame.bmp` exists and shows the color test pattern.
- `SELFTEST dummy PASS`.

3. Test DXGI frame:

```powershell
.\x64\Debug\remote_host.exe --self-test dxgi --adapter 0 --output 0
```

Expected:

- `artifacts\dxgi_frame.bmp` exists and shows the real desktop.
- `SELFTEST dxgi PASS`.

If this fails with `DXGI_ERROR_UNSUPPORTED`, the issue is Desktop Duplication availability for the selected adapter/output/session, not UDP or D3D11 rendering.

4. Test DXGI resize:

```powershell
.\x64\Debug\remote_host.exe --self-test dxgi-resize --adapter 0 --output 0 --width 640 --height 360
```

Expected:

- `artifacts\dxgi_resized_frame.bmp` exists and shows the real desktop downscaled to `640x360`.
- `SELFTEST dxgi-resize PASS`.

5. Test UDP only:

Terminal A:

```powershell
.\x64\Debug\remote_client.exe --self-test udp-recv --bind 0.0.0.0 --udp 48001
```

Terminal B:

```powershell
.\x64\Debug\remote_host.exe --self-test udp-send --target 127.0.0.1 --udp 48001 --width 640 --height 360 --fps 10 --max-mbps 150
```

Expected:

- client receives complete frames.
- `artifacts\udp_received_frame.bmp` exists.
- `SELFTEST udp-recv PASS`.

6. Test renderer only:

```powershell
.\x64\Debug\remote_client.exe --self-test renderer
```

Expected:

- D3D11 window opens and displays the local dummy frame for 3 seconds.
- `SELFTEST renderer PASS`.

7. Full local DXGI raw run:

```powershell
.\x64\Debug\remote_host.exe --mode dxgi --adapter 0 --output 0 --width 640 --height 360 --fps 10 --max-mbps 150 --bind 127.0.0.1 --tcp 48000
.\x64\Debug\remote_client.exe --host 127.0.0.1 --tcp 48000 --udp 48001 --bind 0.0.0.0
```

Expected:

- HostInfo resolution is not `0x0`.
- Client receives UDP packets.
- First complete frame is logged.
- Renderer shows desktop.

Raw BGRA is a diagnostic mode only. Native `1920x1080` BGRA at `30 FPS` is about `249 MB/s` before UDP overhead, so it can exhaust Winsock buffers with `WSAENOBUFS`. Use reduced raw settings like `640x360@10` until H.264 is implemented.

## Debug GUI

After building Debug|x64:

```powershell
.\x64\Debug\remote_debug_gui.exe
```

Use it to:

- choose `dummy` or `dxgi`;
- select DXGI adapter/output;
- edit host bind address, TCP/UDP ports, FPS, and token;
- edit client host IP, bind address, TCP/UDP ports, and token;
- start/stop host and client in separate visible console windows.

Recommended smoke test:

1. Keep mode as `dummy`.
2. Keep host bind `127.0.0.1`, client host `127.0.0.1`, TCP `48000`, client UDP `48001`.
3. Click `Start Both`.

`Start Both` waits until the host TCP port is reachable before it starts the client. If the host exits first, the GUI logs that condition instead of letting the client fail with `Win32Error=10061`.

For DXGI testing, switch mode to `dxgi`, select adapter/output, then click `Start Both`. If the GUI reports that the host exited before TCP became ready, check the host console for the DXGI initialization error and try another adapter/output.

The GUI currently launches DXGI raw preview with explicit safe raw settings: `--width 640 --height 360 --max-mbps 150`. This prevents accidental native `1080p` raw UDP streaming.
