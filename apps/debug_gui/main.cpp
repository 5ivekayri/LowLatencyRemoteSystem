#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dxgi1_6.h>
#include <commctrl.h>
#include <filesystem>
#include <format>
#include <sstream>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace {

struct OutputItem {
    UINT index = 0;
    std::string name;
};

struct AdapterItem {
    UINT index = 0;
    std::string name;
    std::vector<OutputItem> outputs;
};

struct ChildProcess {
    PROCESS_INFORMATION pi{};

    bool running() const {
        if (!pi.hProcess) {
            return false;
        }
        DWORD code = 0;
        return GetExitCodeProcess(pi.hProcess, &code) && code == STILL_ACTIVE;
    }

    void closeHandles() {
        if (pi.hThread) {
            CloseHandle(pi.hThread);
            pi.hThread = nullptr;
        }
        if (pi.hProcess) {
            CloseHandle(pi.hProcess);
            pi.hProcess = nullptr;
        }
        pi.dwProcessId = 0;
        pi.dwThreadId = 0;
    }

    void stop() {
        if (running()) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 2000);
        }
        closeHandles();
    }
};

enum ControlId {
    IdHostMode = 100,
    IdHostAdapter,
    IdHostOutput,
    IdHostFps,
    IdHostBind,
    IdHostTcp,
    IdHostUdp,
    IdToken,
    IdStartHost,
    IdStopHost,
    IdListAdapters,

    IdClientHost = 200,
    IdClientBind,
    IdClientTcp,
    IdClientUdp,
    IdStartClient,
    IdStopClient,
    IdStartBoth,
    IdStopBoth,
    IdLog,
};

HWND g_hwnd = nullptr;
HWND g_log = nullptr;
std::vector<AdapterItem> g_adapters;
ChildProcess g_host;
ChildProcess g_client;
int g_startBothAttempts = 0;
constexpr UINT_PTR StartBothTimerId = 1;
constexpr int StartBothMaxAttempts = 40;

std::string WideToUtf8(const wchar_t* text) {
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

void AppendLog(const std::string& text) {
    if (!g_log) {
        return;
    }
    const int len = GetWindowTextLengthA(g_log);
    SendMessageA(g_log, EM_SETSEL, len, len);
    std::string line = text + "\r\n";
    SendMessageA(g_log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

uint16_t ParsePortText(const std::string& text, uint16_t fallback) {
    try {
        const auto value = std::stoul(text);
        if (value > 0 && value <= 65535) {
            return static_cast<uint16_t>(value);
        }
    } catch (...) {
    }
    return fallback;
}

std::filesystem::path ExeDirectory() {
    char path[MAX_PATH]{};
    const DWORD size = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (size == 0 || size == std::size(path)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(path).parent_path();
}

std::string GetText(HWND hwnd, int id) {
    char buffer[512]{};
    GetWindowTextA(GetDlgItem(hwnd, id), buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}

int GetComboSelection(HWND hwnd, int id) {
    return static_cast<int>(SendDlgItemMessageA(hwnd, id, CB_GETCURSEL, 0, 0));
}

void SetText(HWND hwnd, int id, const char* text) {
    SetWindowTextA(GetDlgItem(hwnd, id), text);
}

HWND AddLabel(HWND parent, const char* text, int x, int y, int w, int h) {
    return CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, GetModuleHandleA(nullptr), nullptr);
}

HWND AddEdit(HWND parent, int id, const char* text, int x, int y, int w, int h) {
    return CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", text, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleA(nullptr), nullptr);
}

HWND AddButton(HWND parent, int id, const char* text, int x, int y, int w, int h) {
    return CreateWindowExA(0, "BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleA(nullptr), nullptr);
}

HWND AddCombo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExA(WS_EX_CLIENTEDGE, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleA(nullptr), nullptr);
}

std::vector<AdapterItem> EnumerateAdapters() {
    std::vector<AdapterItem> adapters;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        return adapters;
    }
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        AdapterItem item;
        item.index = adapterIndex;
        item.name = WideToUtf8(desc.Description);

        for (UINT outputIndex = 0;; ++outputIndex) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, output.GetAddressOf());
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                break;
            }
            DXGI_OUTPUT_DESC outputDesc{};
            output->GetDesc(&outputDesc);
            const LONG width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
            const LONG height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
            item.outputs.push_back({outputIndex, std::format("{} ({}x{})", WideToUtf8(outputDesc.DeviceName), width, height)});
        }
        adapters.push_back(std::move(item));
    }
    return adapters;
}

void PopulateAdapters(HWND hwnd) {
    g_adapters = EnumerateAdapters();
    HWND combo = GetDlgItem(hwnd, IdHostAdapter);
    SendMessageA(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& adapter : g_adapters) {
        const std::string label = std::format("{}: {}", adapter.index, adapter.name);
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!g_adapters.empty()) {
        SendMessageA(combo, CB_SETCURSEL, 0, 0);
    }
    SendMessageA(hwnd, WM_COMMAND, MAKEWPARAM(IdHostAdapter, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
    AppendLog(std::format("Enumerated {} DXGI adapter(s)", g_adapters.size()));
}

void PopulateOutputs(HWND hwnd) {
    HWND outputCombo = GetDlgItem(hwnd, IdHostOutput);
    SendMessageA(outputCombo, CB_RESETCONTENT, 0, 0);
    const int adapterSel = GetComboSelection(hwnd, IdHostAdapter);
    if (adapterSel < 0 || adapterSel >= static_cast<int>(g_adapters.size())) {
        return;
    }
    const auto& outputs = g_adapters[adapterSel].outputs;
    for (const auto& output : outputs) {
        const std::string label = std::format("{}: {}", output.index, output.name);
        SendMessageA(outputCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!outputs.empty()) {
        SendMessageA(outputCombo, CB_SETCURSEL, 0, 0);
    }
}

bool StartChild(ChildProcess& child, const std::filesystem::path& exe, const std::string& args) {
    if (child.running()) {
        AppendLog("Process already running: " + exe.filename().string());
        return true;
    }
    child.closeHandles();

    std::string command = "\"" + exe.string() + "\" " + args;
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessA(
        nullptr,
        command.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_CONSOLE,
        nullptr,
        ExeDirectory().string().c_str(),
        &si,
        &pi);
    if (!ok) {
        AppendLog(std::format("CreateProcess failed for {} error={}", exe.filename().string(), GetLastError()));
        return false;
    }
    child.pi = pi;
    AppendLog("Started: " + exe.filename().string() + " " + args);
    return true;
}

bool CanConnectTcp(const std::string& host, uint16_t port) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &results) != 0) {
        WSACleanup();
        return false;
    }

    bool connected = false;
    for (addrinfo* it = results; it != nullptr && !connected; it = it->ai_next) {
        SOCKET socketHandle = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (socketHandle == INVALID_SOCKET) {
            continue;
        }

        u_long nonBlocking = 1;
        ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
        int result = connect(socketHandle, it->ai_addr, static_cast<int>(it->ai_addrlen));
        if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet{};
            FD_ZERO(&writeSet);
            FD_SET(socketHandle, &writeSet);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 120'000;
            result = select(0, nullptr, &writeSet, nullptr, &timeout);
            if (result > 0) {
                int socketError = 0;
                int socketErrorSize = sizeof(socketError);
                getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorSize);
                connected = socketError == 0;
            }
        } else {
            connected = result == 0;
        }
        closesocket(socketHandle);
    }

    freeaddrinfo(results);
    WSACleanup();
    return connected;
}

std::string CurrentHostArgs(HWND hwnd) {
    const int modeSel = GetComboSelection(hwnd, IdHostMode);
    const std::string mode = modeSel == 1 ? "dxgi" : "dummy";
    const int adapterSel = GetComboSelection(hwnd, IdHostAdapter);
    const int outputSel = GetComboSelection(hwnd, IdHostOutput);
    const UINT adapter = adapterSel >= 0 && adapterSel < static_cast<int>(g_adapters.size()) ? g_adapters[adapterSel].index : 0;
    UINT output = 0;
    if (adapterSel >= 0 && adapterSel < static_cast<int>(g_adapters.size()) && outputSel >= 0 && outputSel < static_cast<int>(g_adapters[adapterSel].outputs.size())) {
        output = g_adapters[adapterSel].outputs[outputSel].index;
    }

    const std::string rawLimits = mode == "dxgi" ? " --width 640 --height 360 --max-mbps 150" : "";
    return std::format(
        "--mode {} --adapter {} --output {} --fps {} --bind {} --tcp {} --udp {} --token {}",
        mode,
        adapter,
        output,
        GetText(hwnd, IdHostFps),
        GetText(hwnd, IdHostBind),
        GetText(hwnd, IdHostTcp),
        GetText(hwnd, IdHostUdp),
        GetText(hwnd, IdToken)) + rawLimits;
}

std::string CurrentClientArgs(HWND hwnd) {
    return std::format(
        "--host {} --bind {} --tcp {} --udp {} --token {}",
        GetText(hwnd, IdClientHost),
        GetText(hwnd, IdClientBind),
        GetText(hwnd, IdClientTcp),
        GetText(hwnd, IdClientUdp),
        GetText(hwnd, IdToken));
}

void StartHost(HWND hwnd) {
    StartChild(g_host, ExeDirectory() / "remote_host.exe", CurrentHostArgs(hwnd));
}

void StartClient(HWND hwnd) {
    StartChild(g_client, ExeDirectory() / "remote_client.exe", CurrentClientArgs(hwnd));
}

void StopHost() {
    g_host.stop();
    AppendLog("Stopped remote_host");
}

void StopClient() {
    g_client.stop();
    AppendLog("Stopped remote_client");
}

void CreateUi(HWND hwnd) {
    AddLabel(hwnd, "Host", 16, 12, 120, 22);
    AddLabel(hwnd, "Mode", 16, 42, 70, 22);
    HWND mode = AddCombo(hwnd, IdHostMode, 90, 38, 140, 120);
    SendMessageA(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("dummy"));
    SendMessageA(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("dxgi"));
    SendMessageA(mode, CB_SETCURSEL, 0, 0);

    AddLabel(hwnd, "Adapter", 16, 74, 70, 22);
    AddCombo(hwnd, IdHostAdapter, 90, 70, 360, 180);
    AddButton(hwnd, IdListAdapters, "Refresh", 460, 70, 80, 24);
    AddLabel(hwnd, "Output", 16, 106, 70, 22);
    AddCombo(hwnd, IdHostOutput, 90, 102, 360, 160);

    AddLabel(hwnd, "FPS", 16, 138, 70, 22);
    AddEdit(hwnd, IdHostFps, "30", 90, 134, 80, 24);
    AddLabel(hwnd, "Bind", 190, 138, 50, 22);
    AddEdit(hwnd, IdHostBind, "127.0.0.1", 240, 134, 130, 24);
    AddLabel(hwnd, "TCP", 390, 138, 40, 22);
    AddEdit(hwnd, IdHostTcp, "48000", 430, 134, 70, 24);
    AddLabel(hwnd, "UDP", 510, 138, 40, 22);
    AddEdit(hwnd, IdHostUdp, "47991", 550, 134, 70, 24);

    AddLabel(hwnd, "Token", 16, 170, 70, 22);
    AddEdit(hwnd, IdToken, "dev-token", 90, 166, 180, 24);
    AddButton(hwnd, IdStartHost, "Start Host", 290, 166, 100, 28);
    AddButton(hwnd, IdStopHost, "Stop Host", 400, 166, 100, 28);

    AddLabel(hwnd, "Client", 16, 214, 120, 22);
    AddLabel(hwnd, "Host IP", 16, 244, 70, 22);
    AddEdit(hwnd, IdClientHost, "127.0.0.1", 90, 240, 130, 24);
    AddLabel(hwnd, "Bind", 240, 244, 50, 22);
    AddEdit(hwnd, IdClientBind, "0.0.0.0", 290, 240, 120, 24);
    AddLabel(hwnd, "TCP", 430, 244, 40, 22);
    AddEdit(hwnd, IdClientTcp, "48000", 470, 240, 70, 24);
    AddLabel(hwnd, "UDP", 550, 244, 40, 22);
    AddEdit(hwnd, IdClientUdp, "48001", 590, 240, 70, 24);

    AddButton(hwnd, IdStartClient, "Start Client", 90, 274, 110, 28);
    AddButton(hwnd, IdStopClient, "Stop Client", 210, 274, 110, 28);
    AddButton(hwnd, IdStartBoth, "Start Both", 340, 274, 110, 28);
    AddButton(hwnd, IdStopBoth, "Stop Both", 460, 274, 110, 28);

    g_log = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 16, 320, 740, 220, hwnd, reinterpret_cast<HMENU>(IdLog), GetModuleHandleA(nullptr), nullptr);
    PopulateAdapters(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateUi(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IdHostAdapter:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                PopulateOutputs(hwnd);
            }
            break;
        case IdListAdapters:
            PopulateAdapters(hwnd);
            break;
        case IdStartHost:
            StartHost(hwnd);
            break;
        case IdStopHost:
            StopHost();
            break;
        case IdStartClient:
            StartClient(hwnd);
            break;
        case IdStopClient:
            StopClient();
            break;
        case IdStartBoth:
            StartHost(hwnd);
            g_startBothAttempts = 0;
            AppendLog("Waiting for host TCP port before starting client...");
            SetTimer(hwnd, StartBothTimerId, 250, nullptr);
            break;
        case IdStopBoth:
            StopClient();
            StopHost();
            break;
        }
        return 0;
    case WM_TIMER:
        if (wp == StartBothTimerId) {
            if (!g_host.running()) {
                KillTimer(hwnd, StartBothTimerId);
                AppendLog("Host exited before TCP became ready. Check the host console; DXGI init may have failed.");
                return 0;
            }

            const auto clientHost = GetText(hwnd, IdClientHost);
            const auto tcpPort = ParsePortText(GetText(hwnd, IdClientTcp), 48000);
            if (CanConnectTcp(clientHost, tcpPort)) {
                KillTimer(hwnd, StartBothTimerId);
                AppendLog(std::format("Host TCP is ready at {}:{}", clientHost, tcpPort));
                StartClient(hwnd);
                return 0;
            }

            ++g_startBothAttempts;
            if (g_startBothAttempts >= StartBothMaxAttempts) {
                KillTimer(hwnd, StartBothTimerId);
                AppendLog(std::format("Timed out waiting for host TCP at {}:{}", clientHost, tcpPort));
            }
        }
        return 0;
    case WM_DESTROY:
        StopClient();
        StopHost();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "RemoteDebugGui";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(0, wc.lpszClassName, "LowLatencyRemoteSystem Debug Launcher", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 800, 610, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) {
        return 1;
    }

    ShowWindow(g_hwnd, show);
    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
