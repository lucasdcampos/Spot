#include "spot/core/log.h"
#include "spot/core/paths.h"
#include "spot/platform/crash_handler_internal.h"

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <mutex>
#include <string>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// DbgHelp needs windows.h first.
#include <dbghelp.h>

namespace Spot::Detail {

namespace {

constexpr DWORD CppExceptionCode = 0xE06D7363;
constexpr int MaxFrames = 64;

LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;

// DbgHelp is single-threaded.
std::mutex& GetSymbolsMutex() {
    static std::mutex mutex;
    return mutex;
}

void InitializeSymbols(HANDLE process) {
    static bool initialized = false;
    if (!initialized) {
        SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        // Loaded at the crash, so modules loaded since startup, such as game modules, are found too.
        initialized = SymInitialize(process, nullptr, TRUE) != FALSE;
    }
}

std::string GetModuleName(DWORD64 address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(address), &module)) {
        return "?";
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    return PathToUtf8(std::filesystem::path(path).filename()) +
           std::format("+0x{:x}", address - reinterpret_cast<DWORD64>(module));
}

// "module+0x1234 Function+0x12 (file.cpp:42)", with what is known of it.
std::string DescribeAddress(HANDLE process, DWORD64 address) {
    std::string text = GetModuleName(address);

    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, address, &displacement, symbol)) {
        text += std::format(" {}+0x{:x}", symbol->Name, displacement);
    }
    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
        text += std::format(" ({}:{})", line.FileName, line.LineNumber);
    }
    return text;
}

std::string WalkStack(HANDLE thread, CONTEXT context) {
    const HANDLE process = GetCurrentProcess();
    const std::scoped_lock lock(GetSymbolsMutex());
    InitializeSymbols(process);

    STACKFRAME64 frame = {};
#if defined(_M_X64) || defined(__x86_64__)
    constexpr DWORD Machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    constexpr DWORD Machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrStack.Offset = context.Sp;
    frame.AddrFrame.Offset = context.Fp;
#else
    constexpr DWORD Machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrStack.Offset = context.Esp;
    frame.AddrFrame.Offset = context.Ebp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;

    std::string stack;
    for (int index = 0; index < MaxFrames; ++index) {
        if (!StackWalk64(Machine, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        stack += std::format("  #{} {}\n", index, DescribeAddress(process, frame.AddrPC.Offset));
    }
    return stack.empty() ? "  (unknown)\n" : stack;
}

std::string DescribeException(const EXCEPTION_RECORD& record) {
    const auto address = reinterpret_cast<DWORD64>(record.ExceptionAddress);
    const std::string where = DescribeAddress(GetCurrentProcess(), address);
    switch (record.ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR: {
        const ULONG_PTR kind = record.NumberParameters >= 2 ? record.ExceptionInformation[0] : 0;
        const char* access = kind == 0 ? "reading" : kind == 1 ? "writing" : "executing";
        const ULONG_PTR target = record.NumberParameters >= 2 ? record.ExceptionInformation[1] : 0;
        return std::format("access violation {} address 0x{:x}, in {}", access, target, where);
    }
    case EXCEPTION_STACK_OVERFLOW:
        return std::format("stack overflow, in {}", where);
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
        return std::format("illegal instruction, in {}", where);
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return std::format("integer division by zero, in {}", where);
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return std::format("misaligned data access, in {}", where);
    default:
        return std::format("exception 0x{:08X}, in {}", record.ExceptionCode, where);
    }
}

void WriteDump(EXCEPTION_POINTERS* exception, DWORD threadId) {
    const std::filesystem::path path = GetCrashReportPath(".dmp");
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION information = {};
    information.ThreadId = threadId;
    information.ExceptionPointers = exception;
    information.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
    const bool written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                           exception ? &information : nullptr, nullptr, nullptr) != FALSE;
    CloseHandle(file);
    if (written) {
        Log::Detail::WriteDuringCrash(LogLevel::Error, "Crash", std::format("Crash dump: '{}'", PathToUtf8(path)));
    }
}

// Reports the crash of the thread on a thread of its own, which has the stack the crashed thread may have run out of.
struct CrashedThread {
    EXCEPTION_POINTERS* exception = nullptr;
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    CONTEXT context = {};
    std::string what;
};

DWORD WINAPI ReportCrashedThread(LPVOID parameter) {
    auto& crashed = *static_cast<CrashedThread*>(parameter);
    if (crashed.what.empty()) {
        crashed.what = DescribeException(*crashed.exception->ExceptionRecord);
    }
    const std::filesystem::path report = ReportCrash(crashed.what, WalkStack(crashed.thread, crashed.context));
    WriteDump(crashed.exception, crashed.threadId);
    ShowCrashMessage(crashed.what, report);
    return 0;
}

void ReportThread(CrashedThread& crashed) {
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &crashed.thread, 0, FALSE,
                    DUPLICATE_SAME_ACCESS);
    crashed.threadId = GetCurrentThreadId();
    if (const HANDLE reporter = CreateThread(nullptr, 1024 * 1024, ReportCrashedThread, &crashed, 0, nullptr)) {
        WaitForSingleObject(reporter, INFINITE);
        CloseHandle(reporter);
    } else {
        ReportCrashedThread(&crashed);
    }
    CloseHandle(crashed.thread);
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* exception) {
    // C++ exceptions go on to the runtime's filter, which calls std::terminate(), whose handler says what they were.
    const bool cppException = exception->ExceptionRecord->ExceptionCode == CppExceptionCode;
    if (!(cppException && s_previousFilter) && BeginCrash()) {
        CrashedThread crashed;
        crashed.exception = exception;
        crashed.context = *exception->ContextRecord;
        ReportThread(crashed);
    }
    return s_previousFilter ? s_previousFilter(exception) : EXCEPTION_EXECUTE_HANDLER;
}

// For the crashes the C runtime catches, which have no exception: reported from where they happened.
void ReportHere(std::string what) {
    if (!BeginCrash()) {
        return;
    }
    CrashedThread crashed;
    crashed.what = std::move(what);
    RtlCaptureContext(&crashed.context);
    ReportThread(crashed);
}

void OnAbort(int /*signal*/) {
    ReportHere("the program aborted");
}

#ifdef _MSC_VER
void OnPureCall() {
    ReportHere("a pure virtual function was called, as happens when code calls an object that was destroyed, or code "
               "built against another version of the engine calls it");
    std::abort();
}

void OnInvalidParameter(const wchar_t* /*expression*/, const wchar_t* /*function*/, const wchar_t* /*file*/,
                        unsigned int /*line*/, std::uintptr_t /*reserved*/) {
    ReportHere("a function of the C runtime was given an invalid parameter");
    std::abort();
}
#endif

} // namespace

void InstallPlatformCrashHandlers() {
    s_previousFilter = SetUnhandledExceptionFilter(OnUnhandledException);
    std::signal(SIGABRT, OnAbort);
#ifdef _MSC_VER
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
    // The crash is reported already; the runtime's own dialog and Windows' report would only hold the process.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    // Keeps stack for the exception handler of a thread that overflowed it.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
}

std::string CaptureStack() {
    CONTEXT context = {};
    RtlCaptureContext(&context);
    return WalkStack(GetCurrentThread(), context);
}

void ShowCrashMessage(std::string_view what, const std::filesystem::path& report) {
    const CrashHandlerConfig& config = GetCrashHandlerConfig();
    if (!config.showMessage) {
        return;
    }
    // The program may have hidden the cursor and held it, as games and flying cameras do.
    ClipCursor(nullptr);
    while (ShowCursor(TRUE) < 0) {
    }
    const std::string name = config.applicationName.empty() ? "The program" : config.applicationName;
    const std::string text =
        report.empty() ? std::format("{} crashed: {}.\n\nThe log has the details.", name, what)
                       : std::format("{} crashed: {}.\n\nThe log has the details, and a report was saved to:\n{}", name,
                                     what, PathToUtf8(report));
    const std::wstring wideText = Utf8ToPath(text).wstring();
    const std::wstring wideTitle = Utf8ToPath(name).wstring();
    MessageBoxW(nullptr, wideText.c_str(), wideTitle.c_str(), MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

} // namespace Spot::Detail
