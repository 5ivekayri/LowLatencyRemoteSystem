# GUI Plan

The current milestone intentionally keeps configuration in CLI so DXGI adapter/output selection and LAN routing can be debugged without a large UI refactor.

Future `remote_host_gui.exe` plan:

- Adapter ComboBox populated from `DxgiAdapterEnumerator`.
- Output ComboBox updated when adapter changes.
- Mode ComboBox: `dummy`, `dxgi`.
- FPS edit.
- Bind address edit.
- TCP port edit.
- Start/Stop buttons that launch or control a visible host session.
- Read-only log textbox.

The GUI must not add hidden mode, persistence, UAC bypass, or unauthenticated control.
