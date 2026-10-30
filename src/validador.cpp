#include <validador.h>

#include <cctype>

namespace juan {

// Divide un comando en argumentos, respetando comillas y escapes.
Result<vec<str>> parse_command(str_vw command) {
    //NewLine characters are not allowed in the command string
    if (command.find('\n') != str_vw::npos) {
        return std::unexpected(Status::PARSE_ERROR);
    }
    
    vec<str> arguments;
    str current_argument;
    char quote = '\0';             // Comilla activa, o '\0' si no hay
    bool argument_started = false; // Permite argumentos vacíos como ""

    for (std::size_t index = 0; index < command.size(); ++index) {
        const char character = command[index];

        // Escape (no aplica dentro de comillas simples)
        if (character == '\\' && quote != '\'') {
            if (index + 1 == command.size()) {
                return std::unexpected(Status::PARSE_ERROR);
            }
            current_argument += command[++index];
            argument_started = true;
        // Dentro de comillas
        } else if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else {
                current_argument += character;
            }
        // Apertura de comillas
        } else if (character == '\'' || character == '"') {
            quote = character;
            argument_started = true;
        // Espacio: termina el argumento actual
        } else if (std::isspace(static_cast<unsigned char>(character))) {
            if (argument_started) {
                arguments.emplace_back(std::move(current_argument));
                current_argument.clear();
                argument_started = false;
            }
        // Carácter normal
        } else {
            current_argument += character;
            argument_started = true;
        }
    }

    // Comillas sin cerrar
    if (quote != '\0') {
        return std::unexpected(Status::PARSE_ERROR);
    }

    // Último argumento
    if (argument_started) {
        arguments.emplace_back(std::move(current_argument));
    }

    return arguments;
}

} // namespace juan