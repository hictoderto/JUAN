#include <parser.h>

namespace juan {

enum struct ParseMode : u8 { NORMAL, SINGLE_QUOTE, DOUBLE_QUOTE };

Result<vec<str>> parse_command(str_vw command) {
    vec<str> arguments;

    bool argument_started = false;

    str current_argument;

    ParseMode mode = ParseMode::NORMAL;
    bool pending_escape = false;

    auto add_c = [&](char c) {
        current_argument += c;
        argument_started = true;
    };

    auto handle_escape = [&](char c) -> bool {
        switch (c) {
        case ' ':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
        case '\v':
        case '\'':
        case '\"':
        case '\\': {
            add_c(c);
        } break;
        case 'a': {
            add_c('\a');
        } break;
        case 'b': {
            add_c('\b');
        } break;
        case 'f': {
            add_c('\f');
        } break;
        case 'n': {
            add_c('\n');
        } break;
        case 'r': {
            add_c('\r');
        } break;
        case 't': {
            add_c('\t');
        } break;
        case 'v': {
            add_c('\v');
        } break;
        default: {
            return false;
        } break;
        }
        return true;
    };

    for (auto c : command) {
        switch (mode) {
        case ParseMode::SINGLE_QUOTE: {
            switch (c) {
            case '\'':
                mode = ParseMode::NORMAL;
                break;
            default:
                add_c(c);
            }
        } break;
        case ParseMode::DOUBLE_QUOTE: {
            if (pending_escape) {
                if (!handle_escape(c))
                    return std::unexpected(Status::PARSE_ERROR);
                pending_escape = false;
            } else {
                switch (c) {
                case '\\': {
                    pending_escape = true;
                } break;
                case '"': {
                    mode = ParseMode::NORMAL;
                } break;
                default: {
                    add_c(c);
                    break;
                }
                }
            }
        } break;
        case ParseMode::NORMAL: {
            if (pending_escape) {
                if (!handle_escape(c))
                    return std::unexpected(Status::PARSE_ERROR);
                pending_escape = false;
            } else {
                switch (c) {
                case '\\': {
                    pending_escape = true;
                } break;
                case '\'': {
                    mode = ParseMode::SINGLE_QUOTE;
                    argument_started = true;
                } break;
                case '"': {
                    mode = ParseMode::DOUBLE_QUOTE;
                    argument_started = true;
                } break;
                case ' ':
                case '\f':
                case '\n':
                case '\r':
                case '\t':
                case '\v': {
                    if (argument_started) {
                        arguments.emplace_back(std::move(current_argument));
                        current_argument = {};
                        argument_started = false;
                    }
                } break;
                default: {
                    add_c(c);
                } break;
                }
            }
        } break;
        default: {
            return std::unexpected(Status::PARSE_ERROR);
        } break;
        }
    }
    if (pending_escape)
        return std::unexpected(Status::PARSE_ERROR);
    if (mode != ParseMode::NORMAL)
        return std::unexpected(Status::PARSE_ERROR);
    if (argument_started)
        arguments.emplace_back(std::move(current_argument));

    return arguments;
}

} // namespace juan
