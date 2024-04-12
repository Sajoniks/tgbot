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
    using JConstObj = JDoc::ConstObject;
    using JConstArray = JDoc::ConstArray;
    using JObj = JDoc::Object;
    using JArray = JDoc::Array;

    template<typename T> using SharedPtr = std::shared_ptr<T>;
    template<typename T> using UniquePtr = std::unique_ptr<T>;
    template<typename T> using Future = std::future<T>;
    template<typename T> using Promise = std::promise<T>;

    using std::make_shared;
    using std::make_unique;

    template<typename T, std::size_t Size, std::size_t Align = alignof(std::max_align_t)>
    class InlinePtr {
    public:

        InlinePtr(const InlinePtr&) = delete;
        InlinePtr(InlinePtr&& ptr) noexcept {
            _data = std::move(ptr._data);
            if (!ptr._null) {
                ptr._null = true;
                _null = false;
            }
        }

        InlinePtr& operator=(const InlinePtr&) = delete;
        InlinePtr& operator=(InlinePtr&& ptr) noexcept {
            if (this != &ptr) {
                _data = std::move(ptr._data);
                if (!ptr._null) {
                    ptr._null = true;
                    _null = false;
                }
            }
            return *this;
        }

        ~InlinePtr() {
            if (!_null) {
                raw()->~T();
                _null = true;
            }
        }

        template<typename... Ts>
        InlinePtr(Ts&&... ts) {
            new (raw()) T{ std::forward<Ts>(ts)... };
            _null = false;
        }

        T* raw() {
            if (!_null) {
                return std::launder(reinterpret_cast<T*>(&_data));
            } else {
                return nullptr;
            }
        }

        const T* raw() const {
            if (!_null) {
                return std::launder(reinterpret_cast<const T*>(&_data));
            } else {
                return nullptr;
            }
        }

        T* operator->() {
            return raw();
        }

        const T* operator->() const {
            return raw();
        }

    private:
        alignas(Align) std::byte _data[Size] { std::byte{ 0 } };
        bool _null { false };
    };

    template<typename T, typename... Ts>
    auto make_pimpl(Ts&&... ts) {
        return InlinePtr<T, sizeof(T), alignof(T)>(std::forward<Ts>(ts)...);
    }

    template<typename T, size_t Size, size_t Align, typename... Ts>
    auto make_pimpl(Ts&&... ts) {
        return InlinePtr<T, Size, Align>(std::forward<Ts>(ts)...);
    }
}