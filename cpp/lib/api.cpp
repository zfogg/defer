#include "api.h"
#include "driver.h"

#include <stdexcept>

namespace defer_tool {

std::string transform_source(const std::string &filename, const std::string &source,
                             const std::vector<std::string> &clang_arguments) {
  if (filename.empty())
    throw std::invalid_argument("a nonempty virtual filename is required");
  ClangIndex index;
  DriverOptions options;
  options.clang_arguments = clang_arguments;
  return transform_content(index, filename, {}, source, options).contents;
}

} // namespace defer_tool
