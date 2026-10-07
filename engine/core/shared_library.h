#pragma once

#include <filesystem>
#include <memory>

namespace Spot {

// Code loaded while the program runs, from a .dll, .so or .dylib, as game modules are.
class SharedLibrary {
public:
    // Null, after logging why, when the library cannot be loaded.
    static std::unique_ptr<SharedLibrary> Load(const std::filesystem::path& path);
    // Unloads the library: nothing of it may be used after.
    ~SharedLibrary();

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    // The address of a function the library exports under the name, such as one declared extern "C"; null when it
    // has none.
    void* GetFunction(const char* name) const;

    const std::filesystem::path& GetPath() const { return m_path; }

private:
    SharedLibrary(void* handle, std::filesystem::path path) : m_handle(handle), m_path(std::move(path)) {}

    void* m_handle;
    std::filesystem::path m_path;
};

} // namespace Spot
