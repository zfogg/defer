#include "clang_support.h"
#include "driver.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);                               \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

int main() {
#ifdef __APPLE__
  std::vector<std::string> defaults;
  defer_tool::add_default_resource_dir(defaults);
  CHECK(std::find(defaults.begin(), defaults.end(), "-isysroot") != defaults.end());
  std::vector<std::string> explicit_sdk = {"-isysroot", "/custom/sdk"};
  defer_tool::add_default_resource_dir(explicit_sdk);
  CHECK(std::count(explicit_sdk.begin(), explicit_sdk.end(), "-isysroot") == 1);
  std::vector<std::string> cross_target = {"--target=wasm32-unknown-unknown"};
  defer_tool::add_default_resource_dir(cross_target);
  CHECK(std::find(cross_target.begin(), cross_target.end(), "-isysroot") == cross_target.end());
#endif
  defer_tool::ClangIndex index;
  const char code[] = "int answer(void) { return 42; }";
  CXUnsavedFile input = {"memory.c", code, sizeof(code) - 1};
  const std::vector<const char *> args = {"-std=c17"};
  defer_tool::TranslationUnit unit(index.get(), "memory.c", args, &input, 1);
  CHECK(unit.errors().empty());
  bool found = false;
  for (const auto cursor : defer_tool::children(clang_getTranslationUnitCursor(unit.get())))
    if (clang_getCursorKind(cursor) == CXCursor_FunctionDecl &&
        defer_tool::consume_string(clang_getCursorSpelling(cursor)) == "answer")
      found = true;
  CHECK(found);
  const char bad_code[] = "int broken( {";
  CXUnsavedFile invalid = {"invalid.c", bad_code, sizeof(bad_code) - 1};
  defer_tool::TranslationUnit bad(index.get(), "invalid.c", args, &invalid, 1);
  CHECK(!bad.errors().empty());
  return 0;
}
