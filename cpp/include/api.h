#pragma once

#include <string>
#include <vector>

namespace defer_tool {

/** Transform one source buffer with the given virtual path and Clang arguments.
 *
 * The virtual path supplies the language, diagnostic locations and relative
 * include directory. Throws std::runtime_error if parsing or transformation
 * fails. The input is never written to disk.
 */
std::string transform_source(const std::string &filename, const std::string &source,
                             const std::vector<std::string> &clang_arguments = {});

} // namespace defer_tool
