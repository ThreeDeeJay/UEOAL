// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – Logger implementation
#include "logger.h"
#include "version.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>

// ─────────────────────────────────────────────────────────────────────────────
Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (m_file.is_open()) {
        m_file << "──────────────── UEOAL session end ────────────────\n";
        m_file.close();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void Logger::Initialize() {
    char path[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableA("UEOAL_LOG_PATH", path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return; // env var absent → logging disabled

    m_file.open(path, std::ios::out | std::ios::app);
    if (!m_file.is_open())
        return;

    m_enabled = true;

    m_file << "\n══════════════════════════════════════════════════════\n"
           << "  UEOAL " UEOAL_VERSION_STRING
           << "  –  built " UEOAL_BUILD_DATE " " UEOAL_BUILD_TIME "\n"
           << "  " UEOAL_PROJECT_URL "\n"
           << "══════════════════════════════════════════════════════\n";
    m_file.flush();
}

// ─────────────────────────────────────────────────────────────────────────────
void Logger::LogV(const char* level, const char* file, int line,
                  const char* fmt, ...) noexcept {
    if (!m_enabled) return;

    // Format user message
    char msg[4096]{};
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // Timestamp with milliseconds
    using Clock = std::chrono::system_clock;
    auto now    = Clock::now();
    auto tt     = Clock::to_time_t(now);
    auto ms     = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

    char tsbuf[32]{};
    struct tm tmi{};
    localtime_s(&tmi, &tt);
    strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", &tmi);

    // Short file name (strip path)
    const char* shortFile = strrchr(file, '\\');
    shortFile = shortFile ? shortFile + 1 : file;
    // Also handle forward slashes
    const char* fwSlash = strrchr(shortFile, '/');
    if (fwSlash) shortFile = fwSlash + 1;

    // Thread ID
    DWORD tid = GetCurrentThreadId();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_file << tsbuf
           << '.' << std::setfill('0') << std::setw(3) << ms.count()
           << " [" << level << "]"
           << " T#" << std::setw(5) << tid
           << " " << shortFile << ":" << std::setw(4) << std::left << line
           << std::right << " | " << msg << '\n';
    m_file.flush();
}
