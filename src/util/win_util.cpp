#if OS_WINDOWS

#include "util.h"

#include <libloaderapi.h>

namespace util {
    std::filesystem::path get_executable_path() {
        return { };
    }
}

#endif