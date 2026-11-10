#include "api.h"
#include "driver.h"

#include <cstdlib>
#include <cstring>
#include <new>
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

extern "C" int defer_transform_source(const char *filename, const char *source, size_t source_size,
                                      const char *const *arguments, size_t argument_count, char **output,
                                      size_t *output_size, char **error) {
  if (output)
    *output = nullptr;
  if (output_size)
    *output_size = 0;
  if (error)
    *error = nullptr;
  try {
    if (!filename || !*filename || (!source && source_size) || (!arguments && argument_count) || !output ||
        !output_size)
      throw std::invalid_argument("invalid source buffer, filename, arguments, or output pointers");
    std::vector<std::string> flags;
    flags.reserve(argument_count);
    for (size_t i = 0; i < argument_count; ++i) {
      if (!arguments[i])
        throw std::invalid_argument("null Clang argument");
      flags.emplace_back(arguments[i]);
    }
    const auto result = defer_tool::transform_source(filename, std::string(source ? source : "", source_size), flags);
    auto *buffer = static_cast<char *>(std::malloc(result.size() + 1));
    if (!buffer)
      throw std::bad_alloc();
    std::memcpy(buffer, result.data(), result.size());
    buffer[result.size()] = '\0';
    *output = buffer;
    *output_size = result.size();
    return 0;
  } catch (const std::exception &exception) {
    if (error) {
      const auto length = std::strlen(exception.what());
      *error = static_cast<char *>(std::malloc(length + 1));
      if (*error)
        std::memcpy(*error, exception.what(), length + 1);
    }
  } catch (...) {
    // No C++ exception may cross the C ABI.
  }
  return 1;
}

extern "C" void defer_free_buffer(char *buffer) { std::free(buffer); }
