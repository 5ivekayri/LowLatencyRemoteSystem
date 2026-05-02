#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <windows.h>

namespace {

bool StartProcessInNewConsole(const std::filesystem::path& exePath, const std::string& args, const std::filesystem::path& workingDirectory) {
    std::string commandLine = "\"" + exePath.string() + "\" " + args;

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};
    const BOOL ok = CreateProcessA(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_CONSOLE,
        nullptr,
        workingDirectory.string().c_str(),
        &startupInfo,
        &processInfo);

    if (!ok) {
        std::cerr << "CreateProcess failed for " << exePath << ", error=" << GetLastError() << '\n';
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

std::filesystem::path ExeDirectory() {
    char path[MAX_PATH]{};
    const DWORD size = GetModuleFileNameA(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (size == 0 || size == std::size(path)) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(path).parent_path();
}

} // namespace

int main(int argc, char** argv) {
    const auto binDir = ExeDirectory();
    const auto hostExe = binDir / "remote_host.exe";
    const auto clientExe = binDir / "remote_client.exe";

    std::string mode = "dxgi";
    if (argc >= 2) {
        const std::string requestedMode = argv[1];
        if (requestedMode == "dxgi" || requestedMode == "dummy") {
            mode = requestedMode;
        }
    }

    std::cout << "Launcher mode: " << mode << " (pass 'dummy' explicitly for test pattern)\n";

    const std::string hostArgs =
        "--mode " + mode + " --adapter 0 --output 0 --fps 30 --bind 127.0.0.1 --token dev-token --tcp 48000 --udp 47991";
    const std::string clientArgs = "--host 127.0.0.1 --bind 0.0.0.0 --token dev-token --tcp 48000 --udp 48001";

    std::cout << "Launching remote_host: " << hostExe << ' ' << hostArgs << '\n';
    if (!StartProcessInNewConsole(hostExe, hostArgs, binDir)) {
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(900));

    std::cout << "Launching remote_client: " << clientExe << ' ' << clientArgs << '\n';
    if (!StartProcessInNewConsole(clientExe, clientArgs, binDir)) {
        return 1;
    }

    std::cout << "Launched host and client. This launcher can be closed.\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}
