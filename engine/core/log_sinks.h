#pragma once

#include "core/log.h"

#include <filesystem>
#include <fstream>

namespace Spot {

class ConsoleSink final : public LogSink {
public:
    ConsoleSink();

    void Write(const LogRecord& record) override;
    void Flush() override;

private:
    bool m_useColors;
};

class FileSink final : public LogSink {
public:
    // Overwrites an existing file and creates missing parent directories.
    explicit FileSink(const std::filesystem::path& path);
    // Out of line because inlining it lets GCC speculatively devirtualize unrelated sink destructors into it,
    // which produces bogus -Warray-bounds warnings.
    ~FileSink() override;

    bool IsOpen() const { return m_file.is_open(); }

    void Write(const LogRecord& record) override;
    void Flush() override;

private:
    std::ofstream m_file;
};

} // namespace Spot
