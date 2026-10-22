#ifndef JUAN_VALIDADOR_H
#define JUAN_VALIDADOR_H

#include "result.h"
#include "types.h"

namespace juan {

// Tokenizes a command line into its executable and arguments.
Result<vec<str>> parse_command(str_vw command);

} // namespace juan

#endif /* ifndef JUAN_VALIDADOR_H */
