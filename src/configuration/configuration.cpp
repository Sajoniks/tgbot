#include "configuration/configuration.h"

#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/reader.h>
#include <rapidjson/istreamwrapper.h>
#include "util.h"
#include "tgapi/fmt/tgbot_fmt.h"

namespace config {

std::string_view Store::operator[](std::string_view key) const {
    auto it = _configTree->find(key);
    if (it != _configTree->end()) {
        return it->second->Value;
    }
    return "";
}

std::vector<std::string_view> Store::values(std::string_view key) const {
    std::vector<std::string_view> result;
    auto it = _configTree->find(key);
    if (it != _configTree->end()) {
        result.reserve(it->second->Children.size());
        for (auto&& ch : it->second->Children) {
            if (ch->Value.empty()) continue;
            result.emplace_back(ch->Value);
        }
    }
    return result;
}

namespace detail {

    namespace json = rapidjson;
    using JDoc = typename json::Document;
    using JObj = typename JDoc::Object;
    using JArr = typename JDoc::Array;
    using JVal = typename JObj::ValueType;

    void build_config_tree_recursive(Store::ConfigTree & tree, const std::shared_ptr<StoreNode>& cur, const std::shared_ptr<StoreNode>& parent, const JVal& val);
    void build_config_tree_recursive(Store::ConfigTree& tree, const std::shared_ptr<StoreNode>& parent, const JArr& arr);
    void build_config_tree_recursive(Store::ConfigTree& tree, const std::shared_ptr<StoreNode>& parent, const JObj& obj);


    void build_config_tree_recursive(Store::ConfigTree & tree,  std::shared_ptr<StoreNode>& cur, const std::shared_ptr<StoreNode>& parent, JVal& val) {
        if (cur == nullptr) {
            throw std::runtime_error("root cannot be null");
        }

        auto& ptrValue = cur->Value;

        switch(val.GetType()){
            case json::kStringType:
                ptrValue = val.GetString();
                break;
            case json::kNumberType:
                if (val.IsUint64()) {
                    ptrValue = std::to_string(val.GetUint64());
                } else if (val.IsInt64()) {
                    ptrValue = std::to_string(val.GetInt64());
                } else if (val.IsUint()) {
                    ptrValue = std::to_string(val.GetUint());
                } else if (val.IsInt()) {
                    ptrValue = std::to_string(val.GetInt());
                } else {
                    throw std::runtime_error("bad number");
                }
                break;
            case json::kFalseType: case json::kTrueType:
                ptrValue = val.GetBool() ? "true" : "false";
                break;

            case json::kNullType:
                ptrValue = "null";
                break;
            case json::kArrayType:
                build_config_tree_recursive(tree, cur, val.GetArray());
                break;
            case json::kObjectType:
                build_config_tree_recursive(tree, cur, val.GetObj());
                break;
        }

        auto* ptr = cur.get();
        tree[ptr->Path] = std::move(cur);
    }

    void build_config_tree_recursive(Store::ConfigTree& tree, const std::shared_ptr<StoreNode>& parent, const JArr& arr) {
        if (arr.Empty()) {
            return;
        }
        if (parent == nullptr) {
            throw std::runtime_error("array cannot be the root of a configuration");
        }

        for (auto it = arr.begin(); it != arr.end(); ++it) {
            std::size_t index = it - arr.begin();
            auto ptr = std::make_shared<StoreNode>();

            ptr->Key = std::to_string(index);
            ptr->Path = fmt::format("{}::{}", parent->Path, index);
            parent->Children.push_back(ptr);

            build_config_tree_recursive(tree, ptr, parent, *it);
        }
    }

    void build_config_tree_recursive(Store::ConfigTree& tree, const std::shared_ptr<StoreNode>& parent, const JObj& obj) {
        if (obj.MemberCount() == 0) {
            return;
        }

        for (auto it = obj.begin(); it != obj.end(); ++it) {
            auto ptr = std::make_shared<StoreNode>();

            if (parent != nullptr) {
                ptr->Path = fmt::format("{}::{}", parent->Path, it->name.GetString());
                parent->Children.push_back(ptr);
            } else {
                ptr->Path = it->name.GetString();
            }
            ptr->Key = it->name.GetString();

            build_config_tree_recursive(tree, ptr, parent, it->value);
        }
    }
}


Store::Store()
    : _configTree{ std::make_shared<ConfigTree>() }{

}

Store Store::from_json(const std::filesystem::path& jsonPath) {
    namespace json = rapidjson;

    const auto path = util::get_executable_path() / jsonPath;
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error(fmt::format(R"("{}": no such file or directory)", jsonPath));
    }

    std::ifstream ifs;
    ifs.open(path);
    if (!ifs.is_open()) {
        throw std::runtime_error(fmt::format(R"("{}": failed to open file)", jsonPath));
    }

    json::Document d;
    json::Reader r;
    json::IStreamWrapper is{ifs};
    d.ParseStream(is);

    Store s;
    detail::build_config_tree_recursive(*s._configTree, nullptr, d.GetObj());

    return s;
}


}