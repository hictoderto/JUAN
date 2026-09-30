#ifndef JUAN_DATABASE_H
#define JUAN_DATABASE_H
#include <filesystem>

#include "result.h"

struct sqlite3;

namespace juan {
using database = sqlite3*;
using path     = std::filesystem::path;
Result<database> init_db(const path& db_file) noexcept;
} // namespace juan

#endif /* ifndef JUAN_DATABASE_H */
