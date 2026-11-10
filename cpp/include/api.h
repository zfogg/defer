#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Transform a source buffer and return an allocated UTF-8 result.
 *
 * The filename is a virtual source path for language detection, diagnostics,
 * and relative include lookup. Arguments are Clang parser flags, without a
 * compiler executable or input path. On success returns zero, sets ``*output``
 * and ``*output_size``, and sets ``*error`` to NULL. On failure returns
 * nonzero, sets ``*output`` to NULL, and optionally returns an allocated
 * diagnostic in ``*error``.
 * Release both output and error using defer_free_buffer, not the caller's free.
 */
int defer_transform_source(const char *filename, const char *source, size_t source_size, const char *const *arguments,
                           size_t argument_count, char **output, size_t *output_size, char **error);

/** Release a buffer returned by defer_transform_source; NULL is accepted. */
void defer_free_buffer(char *buffer);

#ifdef __cplusplus
}

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
#endif
