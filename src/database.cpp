#include <expected>
#include <sqlite3.h>

#include <database.h>
#include <result.h>

namespace juan {
	using std::unexpected;
	Result<database> init_db(const path& db_file) noexcept {
		database db{};
		auto open_result = sqlite3_open(db_file.c_str(), &db);
		if(open_result != SQLITE_OK) {
			// TODO: Add log
			return unexpected{Status::DB_ERROR};
		}
		return db;
	}
}
