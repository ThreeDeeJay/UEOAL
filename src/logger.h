// SPDX-License-Identifier: GPL-3.0-or-later
// UEOAL – verbose logger (enabled via UEOAL_LOG_PATH env var)
#pragma once

#include <cstdarg>
#include <fstream>
#include <mutex>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  Logger
//  Set the environment variable UEOAL_LOG_PATH to an absolute file path to
//  enable verbose logging.  Example:
//      set UEOAL_LOG_PATH=C:\ueoal.log
// ─────────────────────────────────────────────────────────────────────────────
class Logger {
public:
    static Logger& Get();

    /// Called once from DllMain / DLL_PROCESS_ATTACH.
    void Initialize();

    /// Returns true after a valid UEOAL_LOG_PATH is found and file is opened.
    bool IsEnabled() const noexcept { return m_enabled; }

    /// Core logging function – thread-safe, printf-style.
    void LogV(const char* level, const char* file, int line,
              const char* fmt, ...) noexcept;

    ~Logger();

private:
    Logger()                         = default;
    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream m_file;
    std::mutex    m_mutex;
    bool          m_enabled{ false };
};

// ─────────────────────────────────────────────────────────────────────────────
//  Convenience macros
// ─────────────────────────────────────────────────────────────────────────────
#define LOG_INFO(fmt, ...)  Logger::Get().LogV("INFO ", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::Get().LogV("WARN ", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::Get().LogV("ERROR", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) Logger::Get().LogV("DEBUG", __FILE__, __LINE__, fmt, ##__VA_ARGS__)
