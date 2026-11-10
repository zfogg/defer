#pragma once

#include <clang-c/Index.h>
#include <string>

namespace defer_tool {

/** Return transformed UTF-8 bytes, or throw if cleanup cannot be preserved. */
std::string transform(CXTranslationUnit unit, const std::string &path, const std::string &source, bool objc = false);
/** Transform one file in a translation unit without modifying other files. */
std::string transform_translation_unit_file(CXTranslationUnit unit, const std::string &path, const std::string &source,
                                            const std::string &display_path = {}, bool objc = false);
/** Expand supported defer wrapper macros in a translation unit. */
std::string expand_defer_wrappers(CXTranslationUnit unit, const std::string &path, const std::string &source,
                                  bool allow_other_files = false);
/** Explain which cleanup actions apply at a source line. */
std::string explain(CXTranslationUnit unit, const std::string &path, const std::string &source, unsigned line,
                    bool objc = false);
/** Show the transformed function surrounding a source line. */
std::string show_transformed(CXTranslationUnit unit, const std::string &path, const std::string &source, unsigned line,
                             bool objc = false);

} // namespace defer_tool
