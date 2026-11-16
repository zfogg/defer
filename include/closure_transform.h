#pragma once

#include <string>

namespace defer_tool {

/** Lower the supported borrowed recast_closure source syntax and direct calls. */
std::string transform_closures(const std::string &source);

} // namespace defer_tool
