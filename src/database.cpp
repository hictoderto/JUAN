#include "passkey.h"
#include <expected>
#include <sqlite3.h>

#include <database.h>
#include <logger.h>
#include <result.h>
#include <utility>

namespace juan {

Result<DB> DB::connect(const path& db_file, Logger& logger) noexcept {
    using std::unexpected;

    db_conn db{};

    auto res = sqlite3_open_v2(
        db_file.c_str(),
        &db,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE |
        SQLITE_OPEN_EXRESCODE,
        nullptr
    );

    if (res != SQLITE_OK) {
        logger.error(
            "Failed to open datababase connection with error \" {} \"",
            db ? sqlite3_errmsg(db) : sqlite3_errstr(res)
        );

        return unexpected{Status::DB_ERROR};
    }

    return Result<DB>{
        std::in_place,
        db,
        passkey<DB>{}
    };
}

DB::DB(db_conn db, passkey<DB>) noexcept
    : db{db} {
}

sqlite_res DB::cleanup() {
    // TODO: log if cleanup fails
    return sqlite3_close(db);
}

DB::DB(DB&& rhs) noexcept
    : db{std::exchange(rhs.db, nullptr)} {
}

DB& DB::operator=(DB&& rhs) noexcept {
    if (db) {
        sqlite3_close(db);
    }

    db = std::exchange(rhs.db, nullptr);

    return *this;
}

DB::~DB() {
    sqlite3_close(db);
}

} // namespace juan