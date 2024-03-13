#if OS_LINUX

#include <climits>
#include <unistd.h>
#include "util.h"

namespace util {
    std::filesystem::path get_executable_path() {
        char buf[PATH_MAX];
        const auto pathLen = readlink("/proc/self/exe", buf, PATH_MAX);
        return std::filesystem::path{buf}.parent_path();
    }
}

#endif