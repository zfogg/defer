cmake_minimum_required(VERSION 3.25)
project(recast VERSION 0.1.0 LANGUAGES CXX)

option(RECAST_STATIC_LIBCLANG "Link libclang and LLVM statically" OFF)
option(RECAST_FULLY_STATIC_EXECUTABLE "Link the executable without shared libraries (musl Linux)" OFF)
option(RECAST_BUILD_DOCS "Build Recast documentation targets" ON)

include(GNUInstallDirs)
include(CTest)
include(cmake/LLVMDevelopment.cmake)

recast_find_llvm_development()

find_package(Threads REQUIRED)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Recast.cmake")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Recast.cmake" _recast_cmake_content)
string(LENGTH "${_recast_cmake_content}" _recast_cmake_length)
set(RECAST_CMAKE_CONTENT "")
set(_recast_cmake_offset 0)

while(_recast_cmake_offset LESS _recast_cmake_length)
  math(EXPR _recast_cmake_remaining "${_recast_cmake_length} - ${_recast_cmake_offset}")
  if(_recast_cmake_remaining GREATER 8000)
    set(_recast_cmake_chunk_length 8000)
  else()
    set(_recast_cmake_chunk_length ${_recast_cmake_remaining})
  endif()
  string(SUBSTRING "${_recast_cmake_content}" ${_recast_cmake_offset} ${_recast_cmake_chunk_length} _recast_cmake_chunk)
  string(APPEND RECAST_CMAKE_CONTENT "R\"defer_cmake(${_recast_cmake_chunk})defer_cmake\" ")
  math(EXPR _recast_cmake_offset "${_recast_cmake_offset} + ${_recast_cmake_chunk_length}")
endwhile()

configure_file(cmake/embed.h.in generated/defer_cmake.h @ONLY)

set(_core
  lib/api.cpp
  lib/closure_transform.cpp
  lib/driver.cpp
  lib/macro_expansion.cpp
  lib/manifest.cpp
  lib/plan.cpp
  lib/transform.cpp
)

add_library(recast-objects OBJECT ${_core})
target_compile_features(recast-objects PUBLIC cxx_std_23)
target_include_directories(recast-objects PUBLIC include)
if(RECAST_HOST_CXX_INCLUDE_DIRS)
  target_include_directories(recast-objects BEFORE PRIVATE ${RECAST_HOST_CXX_INCLUDE_DIRS})
endif()

if(MSVC)
  target_compile_options(recast-objects PRIVATE /Zc:char8_t-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # GCC 15 exposes filesystem UTF-8 paths as char8_t in C++23, while the
  # libclang/LLVM APIs consumed by Recast still take narrow strings.
  target_compile_options(recast-objects PRIVATE -fno-char8_t)
endif()

set_target_properties(recast-objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
recast_configure_llvm_host_tool(recast-objects)
target_link_libraries(recast-objects PUBLIC recast-llvm-support recast-libclang)

foreach(_kind STATIC SHARED)
  string(TOLOWER ${_kind} _suffix)
  add_library(recast-${_suffix} ${_kind} $<TARGET_OBJECTS:recast-objects>)
  set_target_properties(recast-${_suffix} PROPERTIES OUTPUT_NAME "recast-${_suffix}" WINDOWS_EXPORT_ALL_SYMBOLS ON)
  target_include_directories(recast-${_suffix} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
  target_link_libraries(recast-${_suffix} PUBLIC recast-llvm-support recast-libclang ${CMAKE_DL_LIBS})
endforeach()

add_executable(recast
  src/main.cpp
)

set_target_properties(recast PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_compile_features(recast PRIVATE cxx_std_23)
target_include_directories(recast PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(RECAST_HOST_CXX_INCLUDE_DIRS)
  target_include_directories(recast BEFORE PRIVATE ${RECAST_HOST_CXX_INCLUDE_DIRS})
endif()

if(MSVC)
  target_compile_options(recast PRIVATE /Zc:char8_t-)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(recast PRIVATE -fno-char8_t)
endif()

recast_configure_llvm_host_tool(recast)
target_link_libraries(recast PRIVATE recast-static Threads::Threads)

if(APPLE AND NOT RECAST_STATIC_LIBCLANG)
  set_property(TARGET recast recast-shared APPEND PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:recast-libclang>")
  set_property(TARGET recast recast-shared APPEND PROPERTY INSTALL_RPATH "$<TARGET_FILE_DIR:recast-libclang>")
endif()

if(RECAST_FULLY_STATIC_EXECUTABLE)
  if(NOT RECAST_STATIC_LIBCLANG OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "RECAST_FULLY_STATIC_EXECUTABLE requires static libclang on Linux")
  endif()
  target_link_options(recast PRIVATE -static)
endif()

if(WIN32 AND NOT RECAST_STATIC_LIBCLANG)
  add_custom_command(TARGET recast POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:recast-libclang>" "$<TARGET_FILE_DIR:recast>")
endif()

if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)

  foreach(_test api text clang transform scope_guard manifest)
    add_executable(recast-${_test}-test tests/${_test}_test.cpp)
    target_compile_features(recast-${_test}-test PRIVATE cxx_std_23)
    target_compile_definitions(recast-${_test}-test PRIVATE "RECAST_TEST_INCLUDE=\"${CMAKE_CURRENT_SOURCE_DIR}/include\"")
    recast_configure_llvm_host_tool(recast-${_test}-test)
    target_link_libraries(recast-${_test}-test PRIVATE recast-static Catch2::Catch2)
    if(APPLE AND NOT RECAST_STATIC_LIBCLANG)
      set_property(TARGET recast-${_test}-test APPEND PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:recast-libclang>")
    endif()
    add_test(NAME recast-${_test} COMMAND recast-${_test}-test)
  endforeach()
  add_executable(recast-api-shared-test tests/api_test.cpp)
  target_compile_features(recast-api-shared-test PRIVATE cxx_std_23)
  target_compile_definitions(recast-api-shared-test PRIVATE "RECAST_TEST_INCLUDE=\"${CMAKE_CURRENT_SOURCE_DIR}/include\"")
  recast_configure_llvm_host_tool(recast-api-shared-test)
  target_link_libraries(recast-api-shared-test PRIVATE recast-shared Catch2::Catch2)
  if(APPLE AND NOT RECAST_STATIC_LIBCLANG)
    set_property(TARGET recast-api-shared-test APPEND PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:recast-libclang>")
  endif()
  if(WIN32 AND NOT RECAST_STATIC_LIBCLANG)
    add_custom_command(TARGET recast-api-shared-test POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:recast-libclang>" "$<TARGET_FILE_DIR:recast-api-shared-test>")
  endif()
  add_test(NAME recast-api-shared COMMAND recast-api-shared-test)
endif()

install(TARGETS recast recast-static recast-shared
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES Recast.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/recast)

if(WIN32 AND NOT RECAST_STATIC_LIBCLANG)
  install(FILES "$<TARGET_FILE:recast-libclang>" DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(RECAST_BUILD_DOCS)
  if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(docs)
  endif()
endif()
