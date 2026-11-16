cmake_minimum_required(VERSION 3.25)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(defer_docs LANGUAGES NONE)
endif()
find_program(RECAST_DOCS_UV NAMES uv)
if(NOT RECAST_DOCS_UV)
  message(STATUS "uv not found; the docs target requires uv to build")
  set(RECAST_DOCS_UV uv)
endif()
get_filename_component(_defer_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(RECAST_DOCS_OUTPUT "${CMAKE_BINARY_DIR}/site" CACHE PATH "Generated documentation site")
add_custom_target(docs
  COMMAND "${RECAST_DOCS_UV}" run --project "${_defer_root}" --frozen --group docs python "${CMAKE_CURRENT_LIST_DIR}/build.py" "${RECAST_DOCS_OUTPUT}"
  WORKING_DIRECTORY "${_defer_root}" USES_TERMINAL VERBATIM)
