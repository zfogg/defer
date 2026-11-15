/**
 * @file recast/closure.h
 * @brief Public marker contract for transformed borrowed closures.
 */
#pragma once

#define RECAST_CLOSURE_UNTRANSFORMED() \
  _Static_assert(0, "recast_closure() requires the Recast closure transformation")

#if defined(RECAST_CLOSURE_PARSING)
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
