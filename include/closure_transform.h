#pragma once

#include <string>

namespace defer_tool {

/** Lower the supported borrowed recast_closure/recast_call source syntax. */
std::string transform_closures(const std::string &source);

} // namespace defer_tool
