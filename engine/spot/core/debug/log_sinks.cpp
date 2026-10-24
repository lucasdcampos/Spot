#include "spot/core/debug/log_sinks.h"

#include <fstream>

#include <cstdio>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace Spot {

namespace {

constexpr std::string_view ResetColor = "\x1b[0m";

// Colors are only used on a real terminal so redirected output stays free of escape codes.
bool EnableTerminalColors() {
#ifdef _WIN32
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output == INVALID_HANDLE_VALUE || !GetConsoleMode(output, &mode)) {
        return false;
    }
    return SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::string_view GetColor(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "\x1b[90m";
    case LogLevel::Debug:
        return "\x1b[36m";
    case LogLevel::Info:
        return {};
    case LogLevel::Warning:
        return "\x1b[33m";
    case LogLevel::Error:
        return "\x1b[31m";
    }
    return {};
}

} // namespace

ConsoleSink::ConsoleSink() : m_useColors(EnableTerminalColors()) {
}

void ConsoleSink::Write(const LogRecord& record) {
    auto line = FormatLogRecord(record);
    if (const auto color = m_useColors ? GetColor(record.level) : std::string_view{}; !color.empty()) {
        line = std::format("{}{}{}", color, line, ResetColor);
    }
    line += '\n';

    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

void ConsoleSink::Flush() {
    std::fflush(stdout);
}

FileSink::FileSink(const std::filesystem::path& path) {
    if (path.has_parent_path()) {
        // A failure here surfaces as the file not opening, which IsOpen() reports.
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }
    m_file = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
}

FileSink::~FileSink() = default;

bool FileSink::IsOpen() const {
    return m_file->is_open();
}

void FileSink::Write(const LogRecord& record) {
    if (!m_file->is_open()) {
        return;
    }

    // Every line reaches the file at once, so what led to a crash is there even when the process dies without a
    // chance to flush.
    *m_file << FormatLogRecord(record) << '\n';
    m_file->flush();
}

void FileSink::Flush() {
    m_file->flush();
}

} // namespace Spot
