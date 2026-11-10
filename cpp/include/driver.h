#pragma once

#include "clang_support.h"
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace defer_tool {

/** Options used to parse and transform a translation unit. */
struct DriverOptions {
  std::filesystem::path compilation_database;
  std::filesystem::path header_overlay;
  std::vector<std::filesystem::path> protected_paths;
  std::vector<std::string> clang_arguments;
  bool preamble = false;
};

/** The generated output path and source files that affect it. */
struct FileDependencies {
  std::filesystem::path output;
  std::set<std::string> inputs;
};

/** Transformed source contents and the dependencies used to produce them. */
struct TransformResult {
  FileDependencies dependencies;
  std::string contents;
};

/** Read a file as bytes, throwing on failure. */
std::string read_file(const std::filesystem::path &path);
std::vector<std::string> compilation_arguments(const std::filesystem::path &database,
                                               const std::filesystem::path &output,
                                               const std::filesystem::path &source);
/** Add the default Clang resource directory when no explicit one is supplied. */
void add_default_resource_dir(std::vector<std::string> &arguments);
void write_if_changed(const std::filesystem::path &path, const std::string &contents);
std::filesystem::path loaded_libclang_path();
void verify_libclang(const std::filesystem::path &requested);
FileDependencies transform_file(ClangIndex &index, const std::filesystem::path &source,
                                const std::filesystem::path &output, const DriverOptions &options);
TransformResult transform_content(ClangIndex &index, const std::filesystem::path &source,
                                  const std::filesystem::path &output, std::string content,
                                  const DriverOptions &options);
std::string inspect_file(ClangIndex &index, const std::filesystem::path &source, unsigned line,
                         const DriverOptions &options, bool show_transformed);
void write_depfile(const std::filesystem::path &path, const std::vector<FileDependencies> &files);
void write_dependency_stamp(const FileDependencies &file);
bool validate_header_overlay(const std::filesystem::path &overlay, const FileDependencies &file);

} // namespace defer_tool
