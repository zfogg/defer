#include "api.h"

#include <cstdio>
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
  return 0;
}
