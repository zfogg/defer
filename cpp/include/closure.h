/**
 * @file tooling/recast/closure.h
 * @brief Editor-visible contract for transformed borrowed closures.
 *
 * Closure invocations are source markers consumed by the Recast closure pass.
 * They intentionally fail during an ordinary C compilation so a build cannot
 * silently erase a closure body or produce a non-callable placeholder.
 */
#pragma once

#define RECAST_CLOSURE_UNTRANSFORMED() \
  _Static_assert(0, "recast_closure() requires the Recast closure transformation")

#if defined(RECAST_PARSING)
void __recast_closure_marker(void);
#define recast_closure(name, result_type, parameters, captures, ...) \
  do { __recast_closure_marker(); } while (0)
#define recast_call(...) \
  (__recast_closure_marker(), 0)
#define recast_bind(name, function, ...) \
  do { __recast_closure_marker(); } while (0)
#else
#define recast_closure(name, result_type, parameters, captures, ...) \
  RECAST_CLOSURE_UNTRANSFORMED()
#define recast_call(...) \
  RECAST_CLOSURE_UNTRANSFORMED()
#define recast_bind(name, function, ...) \
  RECAST_CLOSURE_UNTRANSFORMED()
#endif
