#include "sqlite/sqlite.h"
#include "util.h"

#include <fmt/core.h>
#include <sqlite3.h>
#include <stdexcept>

namespace sqlite {

Error::Error(int errorCode, sqlite3* engine)
    : std::runtime_error(sqlite3_errmsg(engine))
    , _ec{ errorCode} {
}
Error::Error(int errorCode, const char* what)
    : std::runtime_error(what)
    , _ec{errorCode} {

}
Error::Error(sqlite3* engine)
    : std::runtime_error(sqlite3_errmsg(engine))
    , _ec(SQLITE_BUSY) {
}
Error::Error(const char* what)
    : std::runtime_error(what)
    , _ec(SQLITE_BUSY) {

}

int Error::error_code() const {
    return _ec;
}

namespace detail {

bool do_read(sqlite3_stmt* stmt, bool& hasRow) {
    hasRow = false;
    const auto rv = sqlite3_step(stmt);
    if (rv == SQLITE_DONE || rv == SQLITE_ROW) {
        hasRow = (rv == SQLITE_ROW);
        return true;
    }
    return false;
}

void bind_value(sqlite3_stmt* stmt, sqlite3* engine, int index, long v) {
    if (sqlite3_bind_int64(stmt, index, v) != SQLITE_OK) {
        throw sqlite::Error(engine);
    }
}

void bind_value(sqlite3_stmt* stmt, sqlite3* engine, int index, double v) {
    if (sqlite3_bind_double(stmt, index, v) != SQLITE_OK) {
        throw sqlite::Error(engine);
    }
}

void bind_value(sqlite3_stmt* stmt, sqlite3* engine, int index, std::string_view v) {
    if (sqlite3_bind_text(stmt, index, v.data(), v.size(), SQLITE_STATIC) != SQLITE_OK) {
        throw sqlite::Error(engine);
    }
}

void bind_value(sqlite3_stmt* stmt, sqlite3* engine, int index, const std::string& v) {
    if (sqlite3_bind_text(stmt, index, v.data(), v.size(), SQLITE_STATIC) != SQLITE_OK) {
        throw sqlite::Error(engine);
    }
}

void bind_value(sqlite3_stmt* stmt, sqlite3* engine, int index, const char* v) {
    if (sqlite3_bind_text(stmt, index, v, -1, SQLITE_STATIC) != SQLITE_OK) {
        throw sqlite::Error(engine);
    }
}

void bind_value(sqlite3_stmt* stmt, sqlite3* engine, int index, float v) {
    bind_value(stmt, engine, index, (double)v);
}

void bind_value(sqlite3_stmt* stmt, sqlite3* engine, int index, int v) {
    bind_value(stmt, engine, index, (long)v);
}

template<> auto fetch_column(sqlite3_stmt* s, std::size_t index) -> double {
    return sqlite3_column_double(s, (int)index);
}

template<> auto fetch_column(sqlite3_stmt* s, std::size_t index) -> long {
    return sqlite3_column_int64(s, (int)index);
}

template<> auto fetch_column(sqlite3_stmt* s, std::size_t index) -> std::string {
    return reinterpret_cast<const char*>( sqlite3_column_text(s, (int)index) );
}

}

Database::Database()
        : _engine{nullptr} {}

Database::~Database() {
    close();
}

void Database::open(std::string_view connectionString) {
    if (connectionString.empty()) {
        throw std::runtime_error("empty connection string");
    }

    const auto fullPath = (util::get_executable_path() / connectionString.data());
    if (!std::filesystem::exists(fullPath) || !std::filesystem::is_regular_file(fullPath)) {
        throw std::runtime_error(fmt::format("Database file does not exist: {}", fullPath.c_str()));
    }

    const auto ec = sqlite3_open(fullPath.c_str(), &_engine);
    if (ec != SQLITE_OK) {
        close();
        throw Error(ec, _engine);
    }

}

void Database::close() {

    if (_engine != nullptr) {
        sqlite3_close(_engine);
        _engine = nullptr;
    }
}

Statement Database::prepare(std::string_view sql) {
    return Statement{_engine, sql.data(), sql.size()};
}

Transaction Database::transaction() {
    return Transaction{ _engine };
}

Statement::Statement(sqlite3* engine, const char* ch, std::size_t num)
    : _stmt{ nullptr}
    , _engine{ engine }
{
    const auto ec = sqlite3_prepare_v2(engine, ch, (int)num, &_stmt, nullptr);
    if (ec != SQLITE_OK) {
        throw Error(ec, _engine);
    }
}

void Statement::execute() {
    const auto ec = sqlite3_step(_stmt);
    if (ec != SQLITE_OK && ec != SQLITE_ROW && ec != SQLITE_DONE) {
        throw Error(ec, _engine);
    }
}

Statement::~Statement() {
    sqlite3_finalize(_stmt);
}

Transaction::Transaction(sqlite3* engine)
    : _engine{ engine} {
    sqlite3_exec(_engine, "BEGIN", nullptr, nullptr, nullptr);
}

void Transaction::commit() {
    if (_reverted) throw Error("Transaction has been already rollback");
    if (!_commited) {
        _commited = true;
        sqlite3_exec(_engine, "COMMIT", nullptr, nullptr, nullptr);
    }
}

void Transaction::rollback() {
    if (_commited) throw Error("Transaction has been already committed");
    if (!_reverted) {
        _reverted = true;
        sqlite3_exec(_engine, "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

Transaction::~Transaction() {
    if (!_reverted && !_commited) {
        rollback();
    }
}

}