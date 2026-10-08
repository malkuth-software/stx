// Diagnostic-only wall-clock instruction sampling of one owned x64 child.
// Suspension perturbs execution: sampled runs are never throughput measurements.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

struct NativeHandle final {
    HANDLE value{nullptr};
    NativeHandle() = default;
    NativeHandle(const NativeHandle&) = delete;
    NativeHandle& operator=(const NativeHandle&) = delete;
    ~NativeHandle() {
        if (value != nullptr && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    }
};

int wmain(const int argc, const wchar_t* const* const argv) {
    if (argc != 6) return 2;
    // All arguments are controlled fixture paths/mode; reject embedded quotes.
    for (int index = 1; index < argc; ++index) {
        if (std::wstring(argv[index]).find(L'"') != std::wstring::npos) return 2;
    }
    std::wstring command = L"\"";
    command += argv[1]; command += L"\" \"";
    command += argv[2]; command += L"\" 1000 3 "; command += argv[3];
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    NativeHandle output;
    output.value = CreateFileW(argv[4], GENERIC_WRITE, FILE_SHARE_READ, &security,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output.value == INVALID_HANDLE_VALUE) return 3;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output.value;
    startup.hStdError = output.value;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION child{};
    // Allocate sample storage before starting the measured child.
    std::vector<std::uint64_t> addresses(200000, 0);
    std::size_t count = 0;
    if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return 4;
    NativeHandle process; process.value = child.hProcess;
    NativeHandle thread; thread.value = child.hThread;
    Sleep(10);
    NativeHandle modules;
    modules.value = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, child.dwProcessId);
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    std::uint64_t image_base = 0;
    if (modules.value != INVALID_HANDLE_VALUE && Module32FirstW(modules.value, &module)) {
        image_base = reinterpret_cast<std::uint64_t>(module.modBaseAddr);
    }
    unsigned int failures = 0;
    while (WaitForSingleObject(process.value, 1) == WAIT_TIMEOUT) {
        if (count == addresses.size()) break;
        if (SuspendThread(thread.value) == static_cast<DWORD>(-1)) { ++failures; continue; }
        CONTEXT context{}; context.ContextFlags = CONTEXT_CONTROL;
        const BOOL accepted = GetThreadContext(thread.value, &context);
        const DWORD resumed = ResumeThread(thread.value);
        if (resumed == static_cast<DWORD>(-1)) {
            // This program owns the child; do not leave a live child suspended.
            TerminateProcess(process.value, 5);
            WaitForSingleObject(process.value, INFINITE);
            return 5;
        }
        if (accepted) { addresses[count] = context.Rip; ++count; }
        else ++failures;
    }
    WaitForSingleObject(process.value, INFINITE);
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.value, &exit_code)) return 6;
    std::FILE* const samples = _wfopen(argv[5], L"w");
    if (samples == nullptr) return 7;
    std::fprintf(samples, "image_base,0x%llx\nfailures,%u\nchild_exit,%lu\nip\n",
                 static_cast<unsigned long long>(image_base), failures, exit_code);
    for (std::size_t index = 0; index < count; ++index) {
        std::fprintf(samples, "0x%llx\n", static_cast<unsigned long long>(addresses[index]));
    }
    const bool write_failed = std::ferror(samples) != 0;
    const int closed = std::fclose(samples);
    if (write_failed || closed != 0) return 7;
    if (exit_code != 0 || count == 0 || image_base == 0) return 8;
    return 0;
}
