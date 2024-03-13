#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <tuple>
#include <stdexcept>

class sqlite3;
class sqlite3_stmt;

namespace sqlite {

class Error : public std::runtime_error {
public:
    Error(int errorCode, sqlite3* engine);
    Error(sqlite3* engine);
    Error(int errorCode, const char* what);
    Error(const char* what);
    int error_code() const;
private:
    int _ec;
};

class Database;
class Transaction;
class Statement;

namespace detail {

    bool do_read(sqlite3_stmt* stmt, bool& hasRow);

    struct type_mapper {
        auto map(long x)    -> long { return x; }
        auto map(int x)     -> long { return x; }
        auto map(short x)   -> long { return x; }
        auto map(bool x)    -> long { return x ? 1 : 0; }
        auto map(float x)   -> double { return x; }
        auto map(double x)  -> double { return x; }
        auto map(const char* x) -> std::string { return std::string{x}; }

        // map from string and string_view types
        template<typename T,
                std::enable_if_t<std::is_constructible_v<std::string_view, T> && !std::is_pointer_v<T>, void**> = nullptr>
        auto map(const T& x) -> std::string { return x; }
    };

#pragma region Fetch

    template<typename T>
    auto fetch_column(sqlite3_stmt*, size_t) -> T;

    template<typename... Ts, size_t... Is>
    auto fetch_columns(sqlite3_stmt* stmt, bool hasRow, std::index_sequence<Is...>) {
        using namespace std;
        static_assert((is_default_constructible_v< decltype( type_mapper().map(declval<Ts>()) ) > && ...), "Types must be default constructible");

        if constexpr (sizeof...(Ts) != 0) {
            if (hasRow) {
                return make_tuple((fetch_column<decltype( type_mapper().map(declval<Ts>()) )>(stmt, Is))...);
            } else {
                return make_tuple(decltype( type_mapper().map(declval<Ts>()) ){}...);
            }
        } else {
            static_assert(sizeof...(Ts) != 0);
        }
    }

    template<typename... Ts>
    auto fetch_columns(sqlite3_stmt* stmt, bool hasRow) {
        using namespace std;
        return fetch_columns<decay_t<Ts>...>(stmt, hasRow, make_index_sequence<sizeof...(Ts)>());
    }

#pragma endregion // Fetch


#pragma region Binding

    void bind_value(sqlite3_stmt*, sqlite3*, int, long);
    void bind_value(sqlite3_stmt*, sqlite3*, int, double);

    void bind_value(sqlite3_stmt* stmt, sqlite3*, int index, int v);;
    void bind_value(sqlite3_stmt* stmt, sqlite3*, int index, float v);;

    void bind_value(sqlite3_stmt*, sqlite3*, int, std::string_view);
    void bind_value(sqlite3_stmt*, sqlite3*, int, const std::string&);
    void bind_value(sqlite3_stmt*, sqlite3*, int, const char*);

#pragma endregion // Binding

}

class Database {
public:
    Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void open(std::string_view connectionString);
    Transaction transaction();
    void close();

    Statement prepare(std::string_view sql);

    ~Database();

private:
    sqlite3* _engine;
};

class Transaction {
public:
    explicit Transaction(sqlite3* engine);

    void commit();
    void rollback();

    ~Transaction();

private:
    sqlite3* _engine { nullptr };
    bool _commited { false };
    bool _reverted { false };
};

/**
 * SQLite result set reader
 * @tparam Args
 */
template<typename... Args>
class ResultSet {

public:

    explicit ResultSet(sqlite3_stmt* stmt)
        : _stmt { stmt }
    {}

    [[nodiscard]] auto fetch()  {
        if (!_canRead) {
            throw Error("No rows to read");
        }
        return detail::fetch_columns<Args...>(_stmt, _canRead);
    }

    [[maybe_unused]] bool read() {
        if (detail::do_read(_stmt, _canRead)) {
            return _canRead;
        }
        return false;
    }

private:
    bool _canRead { false };
    sqlite3_stmt* _stmt { nullptr };
};

/**
 * SQLite prepared Statement
 */
class Statement {

public:

    Statement(sqlite3* engine, const char* ch, std::size_t num);

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    template<typename T>
    Statement& with_value(std::size_t index, const T& value) {
        detail::bind_value(_stmt, _engine, index + 1, value);
        return *this;
    }

    void execute();

    template<typename... Ts>
    [[nodiscard]] auto fetch_one() -> decltype(std::declval<ResultSet<Ts...>>().fetch()) {
        bool anyFetch;
        if (detail::do_read(_stmt, anyFetch)) {
            return detail::fetch_columns<Ts...>(_stmt, anyFetch);
        } else {
            throw Error("No rows to read");
        }
    }

    template<typename... Ts>
    [[nodiscard]] auto fetch() -> ResultSet<Ts...> {
        if constexpr (sizeof...(Ts) != 0) {
            return ResultSet<Ts...>{_stmt};
        } else {
            static_assert(sizeof...(Ts) != 0);
        }
    }

    ~Statement();

private:
    sqlite3_stmt* _stmt;
    sqlite3* _engine;
};

}