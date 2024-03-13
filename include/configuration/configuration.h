#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

namespace config {

namespace detail {

    struct StoreNode {
        std::string Key;
        std::string Path;
        std::string Value;
        std::vector<std::shared_ptr<StoreNode>> Children;
    };
}

class Store final {

    Store();
public:

    using ConfigTree  = typename std::unordered_map<std::string_view, std::shared_ptr<detail::StoreNode>>;

    static Store from_json(const std::filesystem::path& jsonPath);

    Store(const Store&) = default;
    Store(Store&&) = default;
    Store& operator=(const Store&) = default;
    Store& operator=(Store&&) = default;
    ~Store() = default;

    [[nodiscard]] std::string_view operator[](std::string_view key) const;
    [[nodiscard]] std::vector<std::string_view> values(std::string_view key) const;

private:
    std::shared_ptr<ConfigTree> _configTree;
};

}
