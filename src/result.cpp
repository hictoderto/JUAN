#include <result.h>

namespace juan {

std::string_view status_str(const Status& status) noexcept {
    using namespace std::literals;
    switch (status) {
    case Status::OK:
        return "OK"sv;
    case Status::DB_ERROR:
        return "database error"sv;
    case Status::ALLOC_ERROR:
        return "error allocating memory"sv;
    case Status::EPOLL_CREATE_ERROR:
        return "error creating epoll instance"sv;
    case Status::EPOLL_ADD_ERROR:
        return "error adding fd to epoll"sv;
    case Status::EPOLL_MOD_ERROR:
        return "error modifying fd in epoll"sv;
    case Status::EPOLL_DEL_ERROR:
        return "error deleting fd from epoll"sv;
    case Status::EVENT_LOOP_ERROR:
        return "event loop error";
    case Status::OPEN_LOG_ERROR:
        return "error opening log"sv;
    case Status::DB_STMT_PREP_ERROR:
        return "error preparing stmt"sv;
    case Status::PARSE_ERROR:
        return "error parsing command"sv;
    }
    return "invalid status"sv;
}
} // namespace juan
