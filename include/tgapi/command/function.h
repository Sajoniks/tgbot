#pragma once

#include <functional>
#include <cstring>
#include <string>
#include <variant>
#include <vector>
#include <cctype>
#include <type_traits>
#include <stdexcept>
#include <cassert>

namespace tg::function {

enum ArgumentType {
    Integer,
    Float,
    Bool,
    String,
};

#pragma region Type resolving details

namespace detail {
    template<typename T>
    constexpr auto ArgumentOf() {
        using BaseType = std::remove_pointer_t<T>;
        using DecayType = std::decay_t<BaseType>;
        if constexpr (std::is_integral_v<DecayType> && !std::is_same_v<bool, DecayType>) {
            return ArgumentType::Integer;
        } else if constexpr (std::is_floating_point_v<DecayType>) {
            return ArgumentType::Float;
        } else if constexpr (std::is_same_v<DecayType , bool>) {
            return ArgumentType::Bool;
        } else if constexpr (std::is_constructible_v<std::string, DecayType>) {
            return ArgumentType::String;
        }
    }

    template<typename T, size_t I>
    constexpr auto arguments_tuple() {
        return ArgumentOf< typename std::tuple_element<I, T>::type >();
    }

    template<typename T, size_t... I>
    constexpr auto arguments_tuple(std::index_sequence<I...>) {
        return std::make_tuple( arguments_tuple<T, I>()... );
    }

    template<typename... Args>
    constexpr auto arguments_tuple() {
        return arguments_tuple<std::tuple<Args...>>(std::make_index_sequence<sizeof...(Args)>());
    }

    template<typename T, typename U, typename = void>
    struct is_castable : std::false_type {};

    template<typename T, typename U>
    struct is_castable<T, U, std::void_t<decltype(static_cast<U>(std::declval<T>()))>> : std::true_type {};

    template<typename T, typename U, typename = void>
    static constexpr bool is_castable_v = is_castable<T, U>::value;
}

#pragma endregion // Type resolving details

struct ArgumentMapper {

    constexpr auto map(bool x) -> bool { return x; }

    auto map(const char* x) -> std::string { return std::string{ x }; }

    template<typename T, std::enable_if_t<
            std::is_pointer_v<T>,
        void**> = nullptr>
    auto map(T x) {
        using Base = std::remove_pointer_t<T>;
        using Mapped = decltype(map(std::declval<Base>()));
        return (Mapped*)nullptr;
    }

    template<typename T, std::enable_if_t<
            !std::is_pointer_v<T> &&
            std::is_same_v<std::string::value_type, typename T::value_type> &&
            std::is_constructible_v<std::string, T>,
        void**> = nullptr>
    auto map(T x) -> std::string { return std::string{ x }; }

    template<typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_pointer_v<T>, void**> = nullptr>
    constexpr auto map(T x) -> long { return static_cast<long>(x); }

    template<typename T, std::enable_if_t<std::is_floating_point_v<T>, void**> = nullptr>
    constexpr auto map(T x) -> double { return static_cast<double>(x); }
};

class Argument {

public:

    template<typename U>
    struct Tag {};

// NOLINTBEGIN

    /**
     * Construct argument with string value
     * @param s String value
     */
    Argument(std::string s)
        : _storage{ std::move(s) }
        , _t{ ArgumentType::String }
    {}

    /**
     * Construct argument with c-string value
     * @param s String literal value
     */
    Argument(const char* s)
        : _storage{ std::string(s) }
        , _t { ArgumentType::String }
    {}

    /**
     * Construct argument with boolean value
     * @param b Boolean value
     */
    Argument(bool b)
            : _storage{ b }
            , _t{ ArgumentType::Bool }
    {}

    /**
     * Construct argument with long value
     * @param i Long value
     */
    Argument(long i)
            : _storage{ i }
            , _t{ ArgumentType::Integer }
    {}

    /**
     * Construct argument with double value
     * @param f Double value
     */
    Argument(double f)
            : _storage{ f }
            , _t{ ArgumentType::Float }
    {}

    /**
     * Construct argument with optional value T
     * @param value Value or nullptr
     */
    template<typename T>
    Argument(Tag<T>, void* value)
            : _storage{ }
            , _t { detail::ArgumentOf<T>() }
    {
        if (value != nullptr) {
            _storage = *(T*)value;
        } else {
            _null = true;
        }
    }

    Argument(const Argument&) = default;
    Argument(Argument&&) = default;

    constexpr bool is_null() const {
        return _storage.valueless_by_exception() || _null;
    }

    Argument& operator=(const Argument&) = default;
    Argument& operator=(Argument&&) = default;

    ~Argument() = default;

    template<typename T>
    auto map() const {

        using MappedType = decltype(ArgumentMapper().map(std::declval<T>()));
        using BaseType = std::decay_t<std::remove_pointer_t<MappedType>>;

        if (_t != detail::ArgumentOf<BaseType>()) {
            throw std::bad_cast();
        }

        if constexpr (std::is_pointer_v<T>) {
            if (is_null()) {
                return (const BaseType*)nullptr;
            } else {
                return &std::get<BaseType>(_storage);
            }
        } else {
            const auto& value = std::get<BaseType>(_storage);
            using RetType = std::conditional_t<detail::is_castable_v<decltype(value), T>, T, decltype(value)>;
            return static_cast<RetType>(value);
        }
    }

// NOLINTEND

private:
    // order preserved as in ArgumentType enum
    std::variant<long, double, bool, std::string> _storage;
    bool _null { false };
    ArgumentType _t;
};

using ArgumentList = std::vector<Argument>;

#pragma region Parsing Details

namespace detail {

    /**
     * Try parse given string to the corresponding argument type
     *
     * @param [in]  arg         Value to parse
     * @param [in]  type        Type to parse into
     * @param [out] outArgs     Out container of arguments
     * @return True if value was parsed and added into the <code>outArgs</code> container
     */
    inline bool parse_single_command_argument(
              std::string& arg
            , const ArgumentType type
            , ArgumentList& outArgs
    )
    {
        switch(type) {
            case Bool:
            {
                if (arg == "null") {
                    outArgs.emplace_back(Argument::Tag<bool>{}, nullptr);
                    return true;
                }

                if (arg.length() == 1) {
                    auto ch = arg[0];
                    if (ch == 'y' || ch =='Y' || ch == '1') {
                        outArgs.emplace_back(true);
                    } else if (ch == 'n' || ch == 'N' || ch == '0') {
                        outArgs.emplace_back(false);
                    }
                    return true;

                } else {
                    static constexpr const char* true_values[] = {
                            "yes", "ok", "on", "true"
                    };
                    static constexpr const char* false_values[] = {
                            "no", "off", "false"
                    };

                    for (const auto& true_value : true_values) {
                        if (arg.length() != std::strlen(true_value)) {
                            continue;
                        }

                        bool match = true;
                        for (std::size_t j = 0; j < arg.length(); ++j) {
                            if (std::tolower(arg[j]) != true_value[j]) {
                                match = false;
                                break;
                            }
                        }

                        if (!match) {
                            continue;
                        }

                        outArgs.emplace_back(true);
                        return true;
                    }

                    for (const auto& false_value : false_values) {
                        if (arg.length() != std::strlen(false_value)) {
                            continue;
                        }

                        bool match = true;
                        for (std::size_t j = 0; j < arg.length(); ++j) {
                            if (std::tolower(arg[j] != false_value[j])) {
                                break;
                            }
                        }

                        if (!match) {
                            continue;
                        }

                        outArgs.emplace_back(false);
                        return true;
                    }
                }
            }
            break;

            case String:
            {
                if (arg == "null") {
                    outArgs.emplace_back(Argument::Tag<std::string>{}, nullptr);
                    return true;
                }

                outArgs.emplace_back(std::move(arg));
                return true;
            }
            break;

            case Float:
            {
                if (arg == "null") {
                    outArgs.emplace_back(Argument::Tag<float>{}, nullptr);
                    return true;
                }

                try {
                    outArgs.emplace_back(std::stod(arg));
                    return true;
                } catch (const std::exception& e) {
                    return false;
                }
            }
            break;

            case Integer:
            {
                if (arg == "null") {
                    outArgs.emplace_back(Argument::Tag<long>{}, nullptr);
                    return true;
                }

                try {
                    outArgs.emplace_back((long)std::stoi(arg));
                    return true;
                } catch (const std::exception& e) {
                    return false;
                }
            }
            break;
        }

        return false;
    }

    template<template<typename> typename Tuple, typename... Types, std::size_t... I>
    bool parse_command_arguments_recurse(
              std::vector<std::string>& args
            , const Tuple<Types...>& argTypes
            , ArgumentList& outArgs
            , std::index_sequence<I...>
    )
    {
        return (parse_single_command_argument(args[I], static_cast<ArgumentType>(std::get<I>(argTypes)), outArgs) && ...);
    }

    template<template<typename> typename Tuple, typename... Types>
    bool parse_command_arguments(std::string_view str, const Tuple<Types...>& argTypes, ArgumentList& outArgs) {
        outArgs.clear();

        std::vector<std::string> split;
        auto prev = str.begin();
        auto cur = prev;
        while (cur <= str.end()) {
            if (cur == str.end() || std::isspace(*cur)) {
                size_t start = (prev - str.begin());
                size_t end   = (cur - str.begin());
                if (start != end) {
                    split.emplace_back(str.substr(start, end - start));
                }
                ++cur;
                prev = cur;
            } else {
                ++cur;
            }
        }

        while(split.size() < sizeof...(Types)) {
            split.emplace_back("null");
        }

        return parse_command_arguments_recurse(split, argTypes, outArgs, std::make_index_sequence<sizeof...(Types)>());
    }
}

#pragma endregion // Parsing details

class FunctionBase {
public:

    [[nodiscard]] virtual size_t num_parameters() const = 0;
    [[nodiscard]] virtual bool parse_arguments(std::string_view str, ArgumentList& args) const = 0;

    FunctionBase(const FunctionBase&) = delete;
    FunctionBase& operator=(const FunctionBase&) = delete;
    virtual ~FunctionBase() = default;

    virtual void operator()(const std::vector<Argument>& args) = 0;

protected:

    FunctionBase() = default;
};


template<typename ReturnType, typename Class, typename... Args>
class Function final : public FunctionBase {

    using FuncPtr = ReturnType (Class::*)(Args...);

    template<size_t... I>
    void invoke_helper(const std::vector<Argument>& args, std::index_sequence<I...>) {
        using Pack = typename std::tuple<Args...>;

        const auto argumentsPack = std::make_tuple( args[I].map<std::tuple_element_t<I, Pack>>()... );
        std::invoke(_func, _obj, std::get<I>(argumentsPack)...);
    }

public:

    static constexpr size_t NumArguments = sizeof...(Args);
    static constexpr auto   ArgumentTypes = detail::arguments_tuple<Args...>();

    Function(Class* obj, FuncPtr funcPtr)
        : _func{ funcPtr }
        , _obj{ obj }
    {}

    [[nodiscard]] std::size_t num_parameters() const override {
        return NumArguments;
    }
    [[nodiscard]] bool parse_arguments(std::string_view str, ArgumentList& args) const override {
        return detail::parse_command_arguments(str, ArgumentTypes, args);
    }

    void operator()(const std::vector<Argument>& args) override {
        if (args.size() >= NumArguments) {
            invoke_helper(args, std::make_index_sequence<sizeof...(Args)>{});
        } else {
            throw std::runtime_error("bad invocation");
        }
    }

private:

    FuncPtr _func;
    Class* _obj;

};

template<typename ReturnType, typename Class>
class Function<ReturnType, Class> final : public FunctionBase {

    using FuncPtr = ReturnType (Class::*)();

    void invoke_helper() {
        std::invoke(_func, _obj);
    }
public:

    Function(Class* obj, FuncPtr funcPtr)
        : _func{ funcPtr }
        , _obj{ obj }
    {}

    [[nodiscard]] std::size_t num_parameters() const override { return 0; }
    [[nodiscard]] bool parse_arguments(std::string_view str, tg::function::ArgumentList &args) const override { return true; }

    void operator()(const std::vector<Argument>& args) override {
        invoke_helper();
    }

private:
    FuncPtr _func;
    Class* _obj;
};



};
