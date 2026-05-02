#pragma once

#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <windows.h>

namespace remote {

enum class LogLevel {
    Info,
    Warn,
    Error,
    Debug,
};

inline const char* ToString(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Debug: return "DEBUG";
    }
    return "INFO";
}

inline void Log(LogLevel level, std::string_view message) {
    static std::mutex mutex;
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::lock_guard lock(mutex);
    std::cout << '[' << ms << "] " << ToString(level) << ' ' << message << '\n';
}

inline const char* HResultName(HRESULT hr) noexcept {
    switch (static_cast<unsigned long>(hr)) {
    case 0x887A0001UL: return "DXGI_ERROR_INVALID_CALL";
    case 0x887A0002UL: return "DXGI_ERROR_NOT_FOUND";
    case 0x887A0004UL: return "DXGI_ERROR_UNSUPPORTED";
    case 0x887A0005UL: return "DXGI_ERROR_DEVICE_REMOVED";
    case 0x887A0006UL: return "DXGI_ERROR_DEVICE_HUNG";
    case 0x887A000AUL: return "DXGI_ERROR_WAS_STILL_DRAWING";
    case 0x887A0026UL: return "DXGI_ERROR_ACCESS_LOST";
    case 0x887A0027UL: return "DXGI_ERROR_WAIT_TIMEOUT";
    case 0x887A002AUL: return "DXGI_ERROR_ACCESS_DENIED";
    default: return "UNKNOWN_HRESULT";
    }
}

template <typename... Args>
inline void Logf(LogLevel level, std::format_string<Args...> fmt, Args&&... args) {
    Log(level, std::format(fmt, std::forward<Args>(args)...));
}

inline void LogHResult(std::string_view context, HRESULT hr) {
    Logf(LogLevel::Error, "{} failed: HRESULT=0x{:08X} ({})", context, static_cast<unsigned long>(hr), HResultName(hr));
}

inline void LogWin32Error(std::string_view context, DWORD error) {
    Logf(LogLevel::Error, "{} failed: Win32Error={}", context, error);
}

} // namespace remote
