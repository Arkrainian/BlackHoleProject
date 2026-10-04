#include "Application/App.h"

#ifdef _WIN32
// Temporary diagnostic aid: logs the SEH exception code and faulting
// address to crash_report.txt before the process dies, so a hard crash
// (access violation, stack overflow, etc. -- the kind that terminates the
// process with no C++ exception and no stderr output) leaves SOME trace to
// debug from, instead of vanishing silently.
#include <cstdio>
#include <windows.h>

LONG WINAPI DiagnosticCrashHandler(EXCEPTION_POINTERS* info) {
    FILE* f = std::fopen("crash_report.txt", "w");
    if (f) {
        std::fprintf(f, "Exception code: 0x%08lX\n", info->ExceptionRecord->ExceptionCode);
        std::fprintf(f, "Faulting address: %p\n", info->ExceptionRecord->ExceptionAddress);
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            info->ExceptionRecord->NumberParameters >= 2) {
            std::fprintf(f, "Access violation %s address 0x%p\n",
                         info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing to" : "reading from",
                         reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
        }
        std::fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH; // still let Windows handle it normally afterward
}
#endif

int main() {
#ifdef _WIN32
    SetUnhandledExceptionFilter(DiagnosticCrashHandler);
#endif
    Application::App app;
    return app.Run();
}
