#include "api.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__);                              \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main() {
  const auto generated = defer_tool::transform_source(
      "api.c", "#include \"defer.h\"\nvoid f(void) { defer((void)0); }\n", {"-I" DEFER_TEST_INCLUDE});
  CHECK(generated.find("__defer_active_") != std::string::npos);
  try {
    (void)defer_tool::transform_source("", "void f(void) {}\n");
    CHECK(false);
  } catch (const std::invalid_argument &) {
  }
  try {
    (void)defer_tool::transform_source("api.c", "#include \"defer.h\"\nvoid f(void) { defer(return); }\n",
                                       {"-I" DEFER_TEST_INCLUDE});
    CHECK(false);
  } catch (const std::runtime_error &) {
  }
  const char *input = "#include \"defer.h\"\nvoid f(void) { defer((void)0); }\n";
  const char *flags[] = {"-I" DEFER_TEST_INCLUDE};
  char *output = nullptr;
  char *error = nullptr;
  size_t output_size = 0;
  CHECK(defer_transform_source("api.c", input, std::strlen(input), flags, 1, &output, &output_size, &error) == 0);
  CHECK(output != nullptr && output_size == std::strlen(output) && error == nullptr);
  CHECK(std::strstr(output, "__defer_active_") != nullptr);
  defer_free_buffer(output);
  output = nullptr;
  CHECK(defer_transform_source("", input, std::strlen(input), flags, 1, &output, &output_size, &error) != 0);
  CHECK(output == nullptr && output_size == 0 && error != nullptr);
  defer_free_buffer(error);
  defer_free_buffer(nullptr);
  return 0;
}
