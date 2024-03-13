#pragma once

#include <rapidjson/document.h>
#include <boost/system.hpp>
#include <future>
#include <cstdint>
#include <string_view>


namespace tg {

    namespace system = boost::system;

    namespace json = rapidjson;
    using JDoc = json::Document;
    using JAlloc = JDoc::AllocatorType;
    using JValue = JDoc::ValueType;
    using JObj = JDoc::ConstObject;
    using JArray = JDoc::ConstArray;

    template<typename T> using SharedPtr = std::shared_ptr<T>;
    template<typename T> using UniquePtr = std::unique_ptr<T>;
    template<typename T> using Future = std::future<T>;
    template<typename T> using Promise = std::promise<T>;

    using std::make_shared;
    using std::make_unique;
}