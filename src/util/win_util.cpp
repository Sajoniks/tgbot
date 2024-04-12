#if OS_WINDOWS

#include "util.h"
#include "windows.h"

namespace util {
    std::filesystem::path get_executable_path() {
        TCHAR Buffer[MAX_PATH];
        const DWORD outputSize = GetModuleFileNameA(NULL, Buffer, MAX_PATH);

        auto separator = DWORD(-1);
        for (DWORD i = outputSize - 1; i != DWORD(-1); --i) {
            if (Buffer[i] == '\\') {
                separator = i;
                break;
            }
        }

        if (separator == DWORD(-1)) {
            separator = outputSize;
        }

        return std::filesystem::path{ Buffer, Buffer + separator };
    }
}

#endif