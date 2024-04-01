#pragma once

#include <fmt/core.h>
#include <filesystem>

template<>
struct fmt::formatter<std::filesystem::path> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx) {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const std::filesystem::path& path, FormatContext& ctx) {
        using UnderlyingType = decltype(path.c_str());
        if constexpr (std::is_same_v<UnderlyingType, const wchar_t*>) {
            return fmt::format_to(ctx.out(), "{}", path.string());
        } else {
            return fmt::format_to(ctx.out(), "{}", path.c_str());
        }
    }
};