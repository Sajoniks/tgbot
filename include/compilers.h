#pragma once

#if defined(__clang__) && !defined(__ibmxl__)
#   define TGBOT_CLANG_VER 1
#else
#   define TGBOT_CLANG_VER 0
#endif

#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER) && !defined(__NVCOMPILER)
#   define TGBOT_GCC_VER 1
#else
#   define TGBOT_GCC_VER 0
#endif

#if defined(__ICL)
#   define TGBOT_ICC_VER __ICL
#elif defined(__INTEL_COMPILER)
#   define TGBOT_ICC_VER __INTEL_COMPILER
#else
#   define TGBOT_ICC_VER 0
#endif

#if defined(_MSC_VER)
#   define TGBOT_MSC_VER _MSC_VER
#else
#   define TGBOT_MSC_VER 0
#endif

