#include "driver.h"
#include "text.h"
#include "transform.h"

#include <algorithm>
#include <cctype>
#include <clang-c/CXCompilationDatabase.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/SHA256.h>
#include <llvm/Support/StringSaver.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <tuple>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace defer_tool {
namespace fs = std::filesystem;
namespace {

struct CompilationDatabase {
  CXCompilationDatabase value = nullptr;
  std::mutex mutex;
  explicit CompilationDatabase(const fs::path &directory) {
    CXCompilationDatabase_Error error;
    value = clang_CompilationDatabase_fromDirectory(directory.u8string().c_str(), &error);
    if (!value || error != CXCompilationDatabase_NoError)
      throw std::runtime_error("cannot load compilation database: " + directory.u8string());
  }
  ~CompilationDatabase() { clang_CompilationDatabase_dispose(value); }
};

std::shared_ptr<CompilationDatabase> cached_compilation_database(const fs::path &directory) {
  struct Entry {
    fs::file_time_type time;
    std::uintmax_t size;
    std::shared_ptr<CompilationDatabase> database;
  };
  static std::mutex mutex;
  static std::map<fs::path, Entry> cache;
  std::lock_guard<std::mutex> lock(mutex);
  const auto file = directory / "compile_commands.json";
  const auto time = fs::last_write_time(file);
  const auto size = fs::file_size(file);
  const auto found = cache.find(directory);
  if (found != cache.end() && found->second.time == time && found->second.size == size)
    return found->second.database;
  auto database = std::make_shared<CompilationDatabase>(directory);
  cache.insert_or_assign(directory, Entry{time, size, database});
  return database;
}

std::string absolute_path_key(const fs::path &path) {
  auto key = fs::absolute(path).generic_u8string();
#ifdef _WIN32
  std::transform(key.begin(), key.end(), key.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#endif
  return key;
}

std::string database_path_key(const fs::path &path) {
  return absolute_path_key(fs::weakly_canonical(fs::absolute(path)));
}

std::size_t exact_command_count(const fs::path &directory, CXCompilationDatabase database, const fs::path &file) {
  struct Entry {
    fs::file_time_type time;
    std::uintmax_t size;
    std::map<std::string, std::size_t> counts;
  };
  static std::mutex mutex;
  static std::map<std::string, Entry> cache;
  std::lock_guard<std::mutex> lock(mutex);
  const auto filename = directory / "compile_commands.json";
  const auto key = absolute_path_key(filename);
  const auto time = fs::last_write_time(filename);
  const auto size = fs::file_size(filename);
  auto found = cache.find(key);
  if (found == cache.end() || found->second.time != time || found->second.size != size) {
    Entry entry{time, size, {}};
    std::map<std::string, fs::path> canonical_parents;
    auto commands = clang_CompilationDatabase_getAllCompileCommands(database);
    if (commands) {
      for (unsigned i = 0; i < clang_CompileCommands_getSize(commands); ++i) {
        const auto command = clang_CompileCommands_getCommand(commands, i);
        const auto workdir = fs::u8path(consume_string(clang_CompileCommand_getDirectory(command)));
        const auto source = fs::u8path(consume_string(clang_CompileCommand_getFilename(command)));
        const auto absolute = fs::absolute(workdir / source);
        const auto parent_key = absolute_path_key(absolute.parent_path());
        auto parent = canonical_parents.find(parent_key);
        if (parent == canonical_parents.end())
          parent = canonical_parents.emplace(parent_key, fs::weakly_canonical(absolute.parent_path())).first;
        const auto candidate = parent->second / absolute.filename();
        std::error_code error;
        const auto status = fs::symlink_status(candidate, error);
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
          throw fs::filesystem_error("cannot inspect compilation database path", candidate, error);
        ++entry.counts[!error && fs::is_symlink(status) ? database_path_key(candidate) : absolute_path_key(candidate)];
      }
      clang_CompileCommands_dispose(commands);
    }
    found = cache.insert_or_assign(key, std::move(entry)).first;
  }
  const auto count = found->second.counts.find(database_path_key(file));
  return count == found->second.counts.end() ? 0 : count->second;
}

std::string compiler_query(const std::string &compiler, const std::string &option) {
  static std::map<std::string, std::string> cached;
  static std::mutex cache_mutex;
  std::lock_guard<std::mutex> lock(cache_mutex);
  const auto key = fs::absolute(fs::u8path(compiler)).u8string() + "\n" + option;
  if (cached.count(key))
    return cached.at(key);
  auto program = llvm::sys::findProgramByName(compiler);
  if (!program)
    throw std::runtime_error("cannot find compiler: " + compiler);
  llvm::SmallString<128> temporary;
  int fd = -1;
  if (auto error = llvm::sys::fs::createTemporaryFile("defer-resource", "txt", fd, temporary))
    throw std::runtime_error("cannot create resource directory output: " + error.message());
  if (auto error = llvm::sys::Process::SafelyCloseFileDescriptor(fd))
    throw std::runtime_error("cannot close resource directory output: " + error.message());
  std::string temp_path(temporary.str());
  const llvm::StringRef arguments[] = {*program, option};
  const std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(temp_path), std::nullopt};
  const int result = llvm::sys::ExecuteAndWait(*program, arguments, std::nullopt, redirects);
  std::string resource;
  try {
    resource = read_file(fs::u8path(temp_path));
  } catch (...) {
    std::error_code ignored;
    fs::remove(fs::u8path(temp_path), ignored);
    throw;
  }
  std::error_code ignored;
  fs::remove(fs::u8path(temp_path), ignored);
  while (!resource.empty() && (resource.back() == '\n' || resource.back() == '\r' || resource.back() == ' '))
    resource.pop_back();
  if (result != 0 || resource.empty())
    throw std::runtime_error("compiler query failed: " + compiler + " " + option);
  cached.emplace(key, resource);
  return resource;
}

std::vector<std::string> emscripten_driver_defaults(const std::string &compiler) {
  static std::mutex mutex;
  static std::map<std::string, std::vector<std::string>> cached;
  std::lock_guard<std::mutex> lock(mutex);
  if (const auto found = cached.find(compiler); found != cached.end())
    return found->second;
  auto flags = compiler_query(compiler, "--cflags");
#ifdef _WIN32
  // Emscripten prints shell-quoted Windows paths; GNU tokenization otherwise
  // treats their backslashes as escapes even inside single-quoted arguments.
  std::replace(flags.begin(), flags.end(), '\\', '/');
#endif
  llvm::BumpPtrAllocator allocator;
  llvm::StringSaver saver(allocator);
  llvm::SmallVector<const char *, 16> tokens;
  llvm::cl::TokenizeGNUCommandLine(flags, saver, tokens);
  std::vector<std::string> result;
  std::string sysroot;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const std::string flag(tokens[i]);
    if ((flag == "-target" || flag == "--target" || flag == "--sysroot") && i + 1 < tokens.size()) {
      result.push_back(flag);
      result.emplace_back(tokens[++i]);
      if (flag == "--sysroot")
        sysroot = result.back();
    } else if (flag.rfind("--target=", 0) == 0 || flag.rfind("--sysroot=", 0) == 0) {
      result.push_back(flag);
      if (flag.rfind("--sysroot=", 0) == 0)
        sysroot = flag.substr(10);
    } else if (flag == "-Xclang" && i + 1 < tokens.size() &&
               std::string_view(tokens[i + 1]).rfind("-iwithsysroot", 0) == 0) {
      result.push_back(flag);
      result.emplace_back(tokens[++i]);
    }
  }
  if (sysroot.empty())
    throw std::runtime_error("Emscripten did not report a sysroot: " + compiler);
  if (!fs::exists(fs::u8path(sysroot) / "include" / "emscripten" / "emscripten.h")) {
    const auto program = llvm::sys::findProgramByName(compiler);
    if (!program)
      throw std::runtime_error("cannot find compiler: " + compiler);
    const llvm::StringRef args[] = {*program, "-E", "-x", "c", "-"};
    const std::optional<llvm::StringRef> redirects[] = {llvm::StringRef(""), llvm::StringRef(""), std::nullopt};
    if (llvm::sys::ExecuteAndWait(*program, args, std::nullopt, redirects) != 0)
      throw std::runtime_error("cannot initialize Emscripten sysroot: " + compiler);
  }
  cached.emplace(compiler, result);
  return result;
}

#ifdef __APPLE__
std::string default_macos_sdk() {
  static const std::string sdk = [] {
    if (const auto root = llvm::sys::Process::GetEnv("SDKROOT"))
      if (!root->empty() && *root != "/" && fs::is_directory(fs::u8path(*root)))
        return *root;
    auto program = llvm::sys::findProgramByName("xcrun");
    if (!program)
      return std::string();
    llvm::SmallString<128> temporary;
    int fd = -1;
    if (llvm::sys::fs::createTemporaryFile("defer-sdk", "txt", fd, temporary))
      return std::string();
    llvm::sys::Process::SafelyCloseFileDescriptor(fd);
    const std::string path(temporary.str());
    const llvm::StringRef args[] = {*program, "--sdk", "macosx", "--show-sdk-path"};
    const std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(path), llvm::StringRef("")};
    const int result = llvm::sys::ExecuteAndWait(*program, args, std::nullopt, redirects);
    std::string root;
    if (result == 0)
      root = read_file(fs::u8path(path));
    std::error_code ignored;
    fs::remove(fs::u8path(path), ignored);
    while (!root.empty() && (root.back() == '\n' || root.back() == '\r' || root.back() == ' '))
      root.pop_back();
    return root;
  }();
  return sdk;
}
#endif

std::vector<std::string> compile_arguments(const fs::path &database, const fs::path &generated,
                                           const fs::path &source) {
  auto db = cached_compilation_database(database);
  fs::path selected;
  std::string directory;
  std::vector<std::string> args;
  {
    std::lock_guard<std::mutex> database_lock(db->mutex);
    selected = exact_command_count(database, db->value, generated) ? generated : source;
    if (exact_command_count(database, db->value, selected) != 1)
      throw std::runtime_error("expected one compile command for " + selected.u8string());
    CXCompileCommands commands = clang_CompilationDatabase_getCompileCommands(db->value, selected.u8string().c_str());
    if (!commands || clang_CompileCommands_getSize(commands) != 1) {
      if (commands)
        clang_CompileCommands_dispose(commands);
      throw std::runtime_error("expected one compile command for " + selected.u8string());
    }
    const auto command = clang_CompileCommands_getCommand(commands, 0);
    directory = consume_string(clang_CompileCommand_getDirectory(command));
    for (unsigned i = 0; i < clang_CompileCommand_getNumArgs(command); ++i)
      args.push_back(consume_string(clang_CompileCommand_getArg(command, i)));
    clang_CompileCommands_dispose(commands);
  }
  if (args.empty())
    throw std::runtime_error("empty compile command for " + generated.u8string());
  const auto compiler = args.front();
  const auto working_directory = fs::u8path(directory);
  const auto generated_overlay = fs::u8path(generated.parent_path().u8string() + ".vfs.json");
  std::vector<std::string> result = {"-working-directory", directory, "-iquote", source.parent_path().u8string()};
  const bool emscripten = compiler.find("emcc") != std::string::npos || compiler.find("em++") != std::string::npos;
  if (emscripten) {
    const auto defaults = emscripten_driver_defaults(compiler);
    result.insert(result.end(), defaults.begin(), defaults.end());
  }
  for (std::size_t i = 1; i < args.size();) {
    const auto &value = args[i];
    if (value == "-Xclang" && i + 3 < args.size() && args[i + 1] == "-include-pch" && args[i + 2] == "-Xclang") {
      i += 4;
      continue;
    }
    if (value == "-o" || value == "-MF" || value == "-MT" || value == "-MQ" || value == "/Fo" || value == "/Fd") {
      i += 2;
      continue;
    }
    if (value.rfind("/Fo", 0) == 0 || value.rfind("/Fd", 0) == 0) {
      ++i;
      continue;
    }
    if (value == "-ivfsoverlay" && i + 1 < args.size() &&
        fs::weakly_canonical(working_directory / fs::u8path(args[i + 1])) == fs::weakly_canonical(generated_overlay)) {
      i += 2;
      continue;
    }
    if (value == "-clang:-ivfsoverlay" && i + 1 < args.size() && args[i + 1].rfind("-clang:", 0) == 0 &&
        fs::weakly_canonical(working_directory / fs::u8path(args[i + 1].substr(7))) ==
            fs::weakly_canonical(generated_overlay)) {
      i += 2;
      continue;
    }
    // Emscripten's driver-only settings are present in the browser compile
    // database but are not valid libclang arguments. They belong to emcc's
    // link/driver layer, not to source parsing by the native defer host.
    if (emscripten && value == "-s" && i + 1 < args.size()) {
      i += 2;
      continue;
    }
    if (emscripten &&
        ((value.size() > 2 && value.rfind("-s", 0) == 0 && std::isupper(static_cast<unsigned char>(value[2]))) ||
         value == "-msimd128" || value == "-Wno-error=pthreads-mem-growth")) {
      ++i;
      continue;
    }
    if ((value == "-I" || value == "-isystem" || value == "-iquote" || value == "-idirafter" || value == "-include" ||
         value == "-imacros" || value == "-isysroot" || value == "--sysroot" || value == "-target" ||
         value == "--target" || value == "-resource-dir" || value == "-Xclang" || value == "-D" || value == "-U" ||
         value == "-x") &&
        i + 1 < args.size()) {
      result.push_back(value);
      result.push_back(args[i + 1]);
      i += 2;
      continue;
    }
    if (value == "--" || value == "-c" || value == "/c" || value == "-MD" || value == "-MMD" || value == "-MP" ||
        (!value.empty() && value[0] != '-' &&
         (absolute_path_key(working_directory / fs::u8path(value)) == absolute_path_key(selected) ||
          database_path_key(working_directory / fs::u8path(value)) == database_path_key(selected)))) {
      ++i;
      continue;
    }
    result.push_back(value);
    ++i;
  }
  const auto compiler_name = fs::path(compiler).filename().u8string();
  const bool gcc_driver = compiler_name == "gcc" || compiler_name == "g++" || compiler_name == "cc" ||
                          compiler_name == "c++";
  if (!gcc_driver &&
      std::none_of(result.begin(), result.end(), [](const auto &arg) { return arg.rfind("-resource-dir", 0) == 0; })) {
    auto compiler_path = fs::u8path(compiler);
    if (compiler_path.is_relative() && compiler_path.has_parent_path())
      compiler_path = (working_directory / compiler_path).lexically_normal();
    result.push_back("-resource-dir");
    result.push_back(compiler_query(compiler_path.u8string(), "-print-resource-dir"));
  }
  return result;
}

} // namespace

std::vector<std::string> compilation_arguments(const fs::path &database, const fs::path &output,
                                               const fs::path &source) {
  return compile_arguments(database, fs::weakly_canonical(fs::absolute(output)),
                           fs::weakly_canonical(fs::absolute(source)));
}

void add_default_resource_dir(std::vector<std::string> &arguments) {
  if (std::none_of(arguments.begin(), arguments.end(),
                   [](const auto &arg) { return arg.rfind("-resource-dir", 0) == 0; }) &&
      llvm::sys::findProgramByName("clang")) {
    arguments.push_back("-resource-dir");
    arguments.push_back(compiler_query("clang", "-print-resource-dir"));
  }
#ifdef __APPLE__
  const bool explicit_target = std::any_of(arguments.begin(), arguments.end(), [](const auto &arg) {
    return arg == "-target" || arg == "--target" || arg.rfind("--target=", 0) == 0 || arg == "-isysroot" ||
           arg.rfind("-isysroot", 0) == 0 || arg.rfind("--sysroot", 0) == 0 || arg == "-nostdinc";
  });
  if (!explicit_target) {
    const auto sdk = default_macos_sdk();
    if (!sdk.empty()) {
      arguments.push_back("-isysroot");
      arguments.push_back(sdk);
    }
  }
#endif
}

static bool is_cpp_source(const fs::path &source, const std::vector<std::string> &arguments) {
  for (std::size_t i = arguments.size(); i > 1; --i)
    if (arguments[i - 2] == "-x")
      return arguments[i - 1] == "c++" || arguments[i - 1] == "c++-header" || arguments[i - 1] == "objective-c++" ||
             arguments[i - 1] == "objective-c++-header";
  const auto extension = source.extension().u8string();
  if (extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".C" || extension == ".hpp" ||
      extension == ".hh" || extension == ".hxx" || extension == ".mm")
    return true;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const auto &arg = arguments[i];
    if (arg.rfind("-std=c++", 0) == 0 || arg.rfind("-std=gnu++", 0) == 0 || arg == "/TP")
      return true;
  }
  return false;
}

static bool is_objc_source(const fs::path &source, const std::vector<std::string> &arguments) {
  for (std::size_t i = arguments.size(); i > 1; --i)
    if (arguments[i - 2] == "-x")
      return arguments[i - 1] == "objective-c" || arguments[i - 1] == "objective-c-header";
  if (source.extension() == ".m")
    return true;
  return false;
}

static void require_objc_exceptions(const std::vector<std::string> &arguments) {
  bool enabled = false;
  for (const auto &arg : arguments) {
    if (arg == "-fobjc-exceptions")
      enabled = true;
    if (arg == "-fno-objc-exceptions" || arg == "-fno-exceptions")
      enabled = false;
  }
  if (!enabled)
    throw std::runtime_error("Objective-C defer requires -fobjc-exceptions in the actual compilation arguments");
}

std::string read_file(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot open " + path.u8string());
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

fs::path loaded_libclang_path() {
#ifdef DEFER_STATIC_LIBCLANG
  // Static libclang has no shared-library pathname. The executable contains
  // its implementation and is a suitable identity for incremental caches.
  auto executable = llvm::sys::fs::getMainExecutable(nullptr, reinterpret_cast<void *>(&loaded_libclang_path));
  if (executable.empty())
    throw std::runtime_error("cannot determine executable path for static libclang");
  return fs::u8path(executable);
#else
  fs::path loaded;
#ifdef _WIN32
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&clang_createIndex), &module))
    throw std::runtime_error("cannot determine the loaded libclang module");
  std::wstring buffer(32768, L'\0');
  const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length == buffer.size())
    throw std::runtime_error("cannot determine the loaded libclang path");
  buffer.resize(length);
  loaded = fs::path(buffer);
#else
  Dl_info info{};
  if (!dladdr(reinterpret_cast<const void *>(&clang_createIndex), &info) || !info.dli_fname)
    throw std::runtime_error("cannot determine the loaded libclang path");
  loaded = fs::u8path(info.dli_fname);
#endif
  return loaded;
#endif
}

void verify_libclang(const fs::path &requested) {
#ifdef DEFER_STATIC_LIBCLANG
  (void)requested;
  throw std::runtime_error("--libclang cannot select a shared library in a statically linked executable");
#else
  if (!fs::is_regular_file(requested))
    throw std::runtime_error("libclang not found: " + requested.u8string());
  const auto loaded = loaded_libclang_path();
  std::error_code error;
  if (fs::equivalent(requested, loaded, error) && !error)
    return;
  if (fs::file_size(requested) != fs::file_size(loaded))
    throw std::runtime_error("--libclang differs from the library loaded by the native executable");
  const auto digest = [](const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
      throw std::runtime_error("cannot read libclang: " + path.u8string());
    llvm::SHA256 hash;
    char buffer[65536];
    while (input.read(buffer, sizeof(buffer)) || input.gcount())
      hash.update(
          llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(buffer), static_cast<std::size_t>(input.gcount())));
    if (input.bad())
      throw std::runtime_error("cannot read libclang: " + path.u8string());
    return hash.final();
  };
  if (digest(requested) != digest(loaded))
    throw std::runtime_error("--libclang differs from the library loaded by the native executable");
#endif
}

void write_if_changed(const fs::path &path, const std::string &contents) {
  if (!path.parent_path().empty())
    fs::create_directories(path.parent_path());
  if (fs::exists(path) && read_file(path) == contents)
    return;
  const auto temporary =
      fs::u8path(path.u8string() + "." + std::to_string(llvm::sys::Process::getProcessId()) + ".tmp");
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(contents.data(), static_cast<std::streamsize>(contents.size())))
      throw std::runtime_error("cannot write " + temporary.u8string());
    file.close();
    if (!file)
      throw std::runtime_error("cannot close " + temporary.u8string());
  }
  if (auto error = llvm::sys::fs::rename(temporary.u8string(), path.u8string()))
    throw std::runtime_error("cannot replace output: " + error.message());
}

static std::unique_ptr<TranslationUnit> parse_wrapped(CXIndex index, const std::string &source_name,
                                                      const std::vector<const char *> &arguments, std::string &content,
                                                      bool preamble = false) {
  for (unsigned pass = 0; pass < 64; ++pass) {
    CXUnsavedFile unsaved{source_name.c_str(), content.data(), static_cast<unsigned long>(content.size())};
    auto unit = std::make_unique<TranslationUnit>(index, source_name, arguments, &unsaved, 1, true, preamble);
    auto expanded = expand_defer_wrappers(unit->get(), source_name, content);
    if (expanded == content)
      return unit;
    content = std::move(expanded);
  }
  throw std::runtime_error("defer wrapper expansion exceeded the recursion limit");
}

static std::string header_overlay_hash(const std::string &name, const std::string &text) {
  llvm::SHA256 hash;
  hash.update(llvm::StringRef(name));
  const uint8_t separator = 0;
  hash.update(llvm::ArrayRef<uint8_t>(&separator, 1));
  hash.update(llvm::StringRef(text));
  return llvm::toHex(hash.final(), true);
}

static void check_output_dependencies(const FileDependencies &file) {
  for (const auto &name : file.inputs) {
    const auto input = fs::u8path(name);
    std::error_code error;
    if (fs::weakly_canonical(file.output) == fs::weakly_canonical(input) ||
        (fs::exists(file.output) && fs::exists(input) && fs::equivalent(file.output, input, error) && !error))
      throw std::runtime_error("output must not overwrite an included file: " + name);
  }
}

bool validate_header_overlay(const fs::path &overlay, const FileDependencies &file) {
  try {
    auto parsed = llvm::json::parse(read_file(overlay));
    if (!parsed) {
      llvm::consumeError(parsed.takeError());
      return false;
    }
    auto *object = parsed->getAsObject();
    if (!object || object->getInteger("version") != 0 || object->getBoolean("use-external-names") != false)
      return false;
    auto *roots = object->getArray("roots");
    if (!roots)
      return false;
    std::set<fs::path> inputs, seen;
    for (const auto &name : file.inputs)
      inputs.insert(fs::weakly_canonical(fs::u8path(name)));
    for (const auto &entry : *roots) {
      auto *root = entry.getAsObject();
      if (!root || root->getString("type") != "file")
        return false;
      const auto name = root->getString("name");
      const auto external = root->getString("external-contents");
      if (!name || !external)
        return false;
      const auto logical = fs::absolute(fs::u8path(name->str())).lexically_normal();
      const auto original = fs::weakly_canonical(logical);
      if (!inputs.count(original) || !seen.insert(logical).second)
        return false;
      const auto generated = fs::u8path(external->str());
      const auto text = read_file(generated);
      const auto expected = fs::u8path(overlay.u8string() + ".headers") /
                            (header_overlay_hash(original.generic_u8string(), text) + original.extension().u8string());
      if (fs::weakly_canonical(generated) != fs::weakly_canonical(expected))
        return false;
    }
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

static FileDependencies transform_with_header_overlay(ClangIndex &index, const fs::path &source, const fs::path &output,
                                                      const std::vector<const char *> &arguments,
                                                      const fs::path &overlay,
                                                      const std::vector<fs::path> &protected_paths, bool objc) {
  const auto source_name = source.u8string();
  const auto source_identity = fs::weakly_canonical(source);
  std::map<std::string, std::string> contents{{source_name, read_file(source)}};
  std::map<std::string, std::set<std::string>> header_aliases;
  std::map<std::string, std::string> header_display_paths;
  std::vector<std::pair<fs::path, fs::path>> root_aliases;
  const auto add_root = [&](fs::path path) {
    if (path.empty())
      return;
    path = fs::absolute(path).lexically_normal();
    const auto canonical = fs::weakly_canonical(path);
    if (path != canonical)
      root_aliases.emplace_back(path, canonical);
  };
  add_root(source.parent_path());
  fs::path working_directory = fs::current_path();
  for (std::size_t i = 0; i + 1 < arguments.size(); ++i)
    if (std::string_view(arguments[i]) == "-working-directory")
      working_directory = fs::u8path(arguments[i + 1]);
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string_view arg(arguments[i]);
    std::string path;
    if ((arg == "-I" || arg == "-iquote" || arg == "-isystem" || arg == "-idirafter") && i + 1 < arguments.size())
      path = arguments[++i];
    else if (arg.size() > 2 && arg.substr(0, 2) == "-I")
      path = std::string(arg.substr(2));
    if (!path.empty()) {
      const auto parsed = fs::u8path(path);
      add_root(parsed.is_absolute() ? parsed : working_directory / parsed);
    }
  }
  std::unique_ptr<TranslationUnit> unit;
  bool stable = false;
  for (unsigned pass = 0; pass < 64; ++pass) {
    std::vector<CXUnsavedFile> unsaved;
    for (const auto &[name, text] : contents)
      unsaved.push_back({name.c_str(), text.data(), static_cast<unsigned long>(text.size())});
    unit = std::make_unique<TranslationUnit>(index.get(), source_name, arguments, unsaved.data(),
                                             static_cast<unsigned>(unsaved.size()));
    std::set<std::string> marker_files;
    std::function<void(CXCursor)> collect = [&](CXCursor cursor) {
      if (clang_getCursorKind(cursor) == CXCursor_CallExpr &&
          consume_string(clang_getCursorSpelling(cursor)) == "__defer_marker") {
        CXFile file = nullptr;
        clang_getExpansionLocation(clang_getCursorLocation(cursor), &file, nullptr, nullptr, nullptr);
        if (file) {
          const auto display = consume_string(clang_getFileName(file));
          const auto logical = fs::absolute(fs::u8path(display)).lexically_normal();
          const auto canonical = fs::weakly_canonical(logical);
          const auto key = canonical == source_identity ? source_name : canonical.u8string();
          marker_files.insert(key);
          header_display_paths.emplace(key, display);
          auto &aliases = header_aliases[key];
          aliases.insert(logical.generic_u8string());
          aliases.insert(canonical.generic_u8string());
          for (const auto &[lexical_root, canonical_root] : root_aliases) {
            for (const auto &[candidate, from, to] :
                 {std::tuple{logical, lexical_root, canonical_root}, std::tuple{logical, canonical_root, lexical_root},
                  std::tuple{canonical, lexical_root, canonical_root},
                  std::tuple{canonical, canonical_root, lexical_root}}) {
              const auto relative = candidate.lexically_relative(from);
              if (!relative.empty() && *relative.begin() != "..")
                aliases.insert((to / relative).lexically_normal().generic_u8string());
            }
          }
        }
      }
      for (auto child : children(cursor))
        collect(child);
    };
    collect(clang_getTranslationUnitCursor(unit->get()));
    bool changed = false;
    for (const auto &name : marker_files) {
      const auto path = fs::u8path(name);
      if (path != source && path.extension() != ".h" && path.extension() != ".hpp" && path.extension() != ".hh" &&
          path.extension() != ".hxx" && path.extension() != ".inc" && path.extension() != ".def")
        throw std::runtime_error("defer in included non-header files is not supported: " + name);
      if (!contents.count(name)) {
        contents.emplace(name, read_file(path));
        changed = true;
      }
    }
    if (changed)
      continue;
    for (auto &[name, text] : contents) {
      auto expanded = expand_defer_wrappers(unit->get(), name, text, true);
      if (expanded != text) {
        text = std::move(expanded);
        changed = true;
      }
    }
    if (!changed) {
      stable = true;
      break;
    }
  }
  if (!stable)
    throw std::runtime_error("header defer wrapper expansion exceeded the recursion limit");
  FileDependencies dependencies{output, {source_name}};
  clang_getInclusions(
      unit->get(),
      [](CXFile file, CXSourceLocation *, unsigned, CXClientData opaque) {
        auto *files = static_cast<std::set<std::string> *>(opaque);
        files->insert(fs::absolute(fs::u8path(consume_string(clang_getFileName(file)))).lexically_normal().u8string());
      },
      &dependencies.inputs);
  check_output_dependencies(dependencies);
  std::map<std::string, std::string> transformed;
  for (const auto &[name, text] : contents) {
    const auto display = name == source_name ? source_name : header_display_paths.at(name);
    transformed.emplace(name, transform_translation_unit_file(unit->get(), name, text, display, objc));
  }
  std::vector<CXUnsavedFile> unsaved;
  for (const auto &[name, text] : transformed)
    unsaved.push_back({name.c_str(), text.data(), static_cast<unsigned long>(text.size())});
  TranslationUnit validation(index.get(), source_name, arguments, unsaved.data(), static_cast<unsigned>(unsaved.size()),
                             false);
  const auto diagnostics = validation.errors();
  if (!diagnostics.empty())
    throw std::runtime_error(std::string(objc ? "generated Objective-C is invalid: " : "generated C is invalid: ") +
                             diagnostics.front());

  const auto conflicts = [&](const fs::path &candidate) {
    std::error_code error;
    if (fs::weakly_canonical(candidate) == fs::weakly_canonical(output) ||
        (fs::exists(candidate) && fs::exists(output) && fs::equivalent(candidate, output, error) && !error))
      return true;
    for (const auto &name : dependencies.inputs) {
      const auto original = fs::u8path(name);
      error.clear();
      if (fs::weakly_canonical(candidate) == fs::weakly_canonical(original) ||
          (fs::exists(candidate) && fs::exists(original) && fs::equivalent(candidate, original, error) && !error))
        return true;
    }
    for (const auto &original : protected_paths) {
      error.clear();
      if (fs::weakly_canonical(candidate) == fs::weakly_canonical(original) ||
          (fs::exists(candidate) && fs::exists(original) && fs::equivalent(candidate, original, error) && !error))
        return true;
    }
    return false;
  };
  if (conflicts(overlay))
    throw std::runtime_error("header overlay must not overwrite an input or output");
  llvm::json::Array roots;
  std::vector<std::pair<fs::path, std::string>> generated;
  for (const auto &[name, text] : transformed) {
    if (name == source_name)
      continue;
    const auto header_name = fs::u8path(name).generic_u8string();
    const auto header = fs::u8path(overlay.u8string() + ".headers") /
                        (header_overlay_hash(header_name, text) + fs::u8path(name).extension().u8string());
    std::error_code error;
    if (conflicts(header) || fs::weakly_canonical(header) == fs::weakly_canonical(overlay) ||
        (fs::exists(header) && fs::exists(overlay) && fs::equivalent(header, overlay, error) && !error))
      throw std::runtime_error("generated header must not overwrite an input, output, or overlay");
    if (fs::exists(header) && read_file(header) != text)
      throw std::runtime_error("generated header path already contains unrelated data: " + header.u8string());
    for (const auto &alias : header_aliases[name])
      roots.push_back(
          llvm::json::Object{{"type", "file"}, {"name", alias}, {"external-contents", header.generic_u8string()}});
    generated.emplace_back(header, text);
  }
  llvm::json::Object mapping{{"version", 0}, {"use-external-names", false}, {"roots", std::move(roots)}};
  const auto json = llvm::formatv("{0:2}", llvm::json::Value(std::move(mapping))).str();
  for (const auto &[path, text] : generated)
    write_if_changed(path, text);
  write_if_changed(overlay, json);
  write_if_changed(output, transformed.at(source_name));
  return dependencies;
}

TransformResult transform_content(ClangIndex &index, const fs::path &source_path, const fs::path &output_path,
                                  std::string content, const DriverOptions &options) {
  if (!options.header_overlay.empty())
    throw std::runtime_error("streaming does not support header overlays");
  const auto source = fs::weakly_canonical(fs::absolute(source_path));
  const auto output = output_path.empty() ? fs::path{} : fs::weakly_canonical(fs::absolute(output_path));
  auto arguments = options.compilation_database.empty()
                       ? options.clang_arguments
                       : compile_arguments(options.compilation_database, output.empty() ? source : output, source);
  add_default_resource_dir(arguments);
  const bool objc = is_objc_source(source, arguments);
  if (objc)
    require_objc_exceptions(arguments);
  arguments.push_back("-DDEFER_PARSING=1");
  std::vector<const char *> raw;
  for (const auto &arg : arguments)
    raw.push_back(arg.c_str());
  const auto source_name = source.u8string();
  if (is_cpp_source(source, arguments)) {
    CXUnsavedFile unsaved{source_name.c_str(), content.data(), static_cast<unsigned long>(content.size())};
    TranslationUnit unit(index.get(), source_name, raw, &unsaved, 1, false);
    const auto diagnostics = unit.errors();
    if (!diagnostics.empty())
      throw std::runtime_error("Clang could not parse C++ source: " + diagnostics.front());
    FileDependencies dependencies{output, {source_name}};
    clang_getInclusions(
        unit.get(),
        [](CXFile file, CXSourceLocation *, unsigned, CXClientData opaque) {
          auto *files = static_cast<std::set<std::string> *>(opaque);
          files->insert(
              fs::absolute(fs::u8path(consume_string(clang_getFileName(file)))).lexically_normal().u8string());
        },
        &dependencies.inputs);
    if (!output.empty())
      check_output_dependencies(dependencies);
    std::string escaped;
    for (char c : source_name) {
      if (c == '\\' || c == '"')
        escaped += '\\';
      escaped += c;
    }
    const auto directive = "\n#line 1 \"" + escaped + "\"\n";
    if (content.compare(0, 3, "\xef\xbb\xbf") == 0)
      content = content.substr(0, 3) + directive + content.substr(3);
    else
      content = directive + content;
    return {std::move(dependencies), std::move(content)};
  }
  auto unit = parse_wrapped(index.get(), source_name, raw, content, options.preamble);
  auto transformed = transform(unit->get(), source.u8string(), content, objc);
  FileDependencies dependencies{output, {source.u8string()}};
  clang_getInclusions(
      unit->get(),
      [](CXFile file, CXSourceLocation *, unsigned, CXClientData opaque) {
        auto *files = static_cast<std::set<std::string> *>(opaque);
        files->insert(fs::absolute(fs::u8path(consume_string(clang_getFileName(file)))).lexically_normal().u8string());
      },
      &dependencies.inputs);
  if (!output.empty())
    check_output_dependencies(dependencies);
  if (options.preamble) {
    unit->reparse(source_name, transformed);
    const auto diagnostics = unit->errors();
    if (!diagnostics.empty())
      throw std::runtime_error(std::string(objc ? "generated Objective-C is invalid: " : "generated C is invalid: ") +
                               diagnostics.front());
  } else {
    validate_generated(index.get(), source_name, raw, transformed, objc);
  }
  return {std::move(dependencies), std::move(transformed)};
}

FileDependencies transform_file(ClangIndex &index, const fs::path &source_path, const fs::path &output_path,
                                const DriverOptions &options) {
  const auto source = options.header_overlay.empty() ? fs::weakly_canonical(fs::absolute(source_path))
                                                     : fs::absolute(source_path).lexically_normal();
  const auto output = fs::weakly_canonical(fs::absolute(output_path));
  if (source == output || (fs::exists(source) && fs::exists(output) && fs::equivalent(source, output)))
    throw std::runtime_error("output must not overwrite the input source");
  if (!options.header_overlay.empty()) {
    auto arguments = options.compilation_database.empty()
                         ? options.clang_arguments
                         : compile_arguments(options.compilation_database, output, source);
    add_default_resource_dir(arguments);
    const bool objc = is_objc_source(source, arguments);
    if (objc)
      require_objc_exceptions(arguments);
    if (is_cpp_source(source, arguments))
      throw std::runtime_error("C++ uses native scope guards and does not require a header overlay");
    arguments.push_back("-DDEFER_PARSING=1");
    std::vector<const char *> raw;
    for (const auto &arg : arguments)
      raw.push_back(arg.c_str());
    return transform_with_header_overlay(index, source, output, raw, options.header_overlay, options.protected_paths,
                                         objc);
  }
  auto result = transform_content(index, source, output, read_file(source), options);
  write_if_changed(output, result.contents);
  return std::move(result.dependencies);
}

std::string inspect_file(ClangIndex &index, const fs::path &source_path, unsigned line, const DriverOptions &options,
                         bool show) {
  if (!options.compilation_database.empty())
    throw std::runtime_error("inspection requires explicit Clang arguments rather than a compilation database");
  const auto source = fs::weakly_canonical(fs::absolute(source_path));
  auto arguments = options.clang_arguments;
  add_default_resource_dir(arguments);
  const bool objc = is_objc_source(source, arguments);
  if (objc)
    require_objc_exceptions(arguments);
  if (is_cpp_source(source, arguments))
    throw std::runtime_error("C++ defer inspection is not supported; C++ uses native scope guards");
  arguments.push_back("-DDEFER_PARSING=1");
  std::vector<const char *> raw;
  for (const auto &arg : arguments)
    raw.push_back(arg.c_str());
  auto content = read_file(source);
  const auto source_name = source.u8string();
  auto unit = parse_wrapped(index.get(), source_name, raw, content);
  validate_generated(index.get(), source_name, raw, transform(unit->get(), source_name, content, objc), objc);
  return show ? show_transformed(unit->get(), source_name, content, line, objc)
              : explain(unit->get(), source_name, content, line, objc);
}

void write_depfile(const fs::path &path, const std::vector<FileDependencies> &files) {
  if (files.empty())
    throw std::runtime_error("cannot write dependencies for an empty batch");
  std::set<std::string> all_inputs;
  for (const auto &file : files)
    all_inputs.insert(file.inputs.begin(), file.inputs.end());
  std::string result = escape_depfile(files.front().output.u8string()) + ":";
  for (const auto &input : all_inputs)
    result += " " + escape_depfile(input);
  result += "\n";
  write_if_changed(path, result);
}

void write_dependency_stamp(const FileDependencies &file) {
  const auto stamp = fs::u8path(file.output.parent_path().u8string() + ".deps");
  std::string contents;
  for (const auto &name : file.inputs) {
    const auto path = fs::u8path(name);
    std::error_code error;
    if (fs::weakly_canonical(stamp) == fs::weakly_canonical(path) ||
        (fs::exists(stamp) && fs::equivalent(stamp, path, error) && !error))
      throw std::runtime_error("dependency stamp must not overwrite an included file");
    error.clear();
    const auto modified = fs::last_write_time(path, error);
    if (error)
      throw std::runtime_error("cannot stat dependency " + name + ": " + error.message());
    const auto size = fs::file_size(path, error);
    if (error)
      throw std::runtime_error("cannot stat dependency " + name + ": " + error.message());
    contents += name + "\t" + std::to_string(static_cast<long long>(modified.time_since_epoch().count())) + "\t" +
                std::to_string(size) + "\t" + fs::weakly_canonical(path).u8string() + "\n";
  }
  write_if_changed(stamp, contents);
}

} // namespace defer_tool
