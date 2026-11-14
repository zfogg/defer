#include "closure_transform.h"

#include <cctype>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace defer_tool {
namespace {

struct Capture {
  std::string name;
  bool reference;
};

struct Closure {
  std::string name;
  std::string result;
  std::string parameters;
  std::string call_parameters;
  std::vector<Capture> captures;
  std::string body;
  std::size_t begin;
  std::size_t end;
  unsigned id;
};

std::size_t matching(std::string_view source, std::size_t open, char left, char right) {
  unsigned depth = 0;
  bool string = false;
  char quote = 0;
  bool line_comment = false;
  bool block_comment = false;
  for (std::size_t i = open; i < source.size(); ++i) {
    const char c = source[i];
    const char next = i + 1 < source.size() ? source[i + 1] : 0;
    if (line_comment) {
      if (c == '\n') line_comment = false;
      continue;
    }
    if (block_comment) {
      if (c == '*' && next == '/') { block_comment = false; ++i; }
      continue;
    }
    if (string) {
      if (c == '\\') { ++i; continue; }
      if (c == quote) string = false;
      continue;
    }
    if (c == '/' && next == '/') { line_comment = true; ++i; continue; }
    if (c == '/' && next == '*') { block_comment = true; ++i; continue; }
    if (c == '"' || c == '\'') { string = true; quote = c; continue; }
    if (c == left) ++depth;
    else if (c == right && --depth == 0) return i;
  }
  throw std::runtime_error("unterminated Recast closure expression");
}

std::vector<std::string> comma_parts(std::string_view text) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  unsigned parens = 0, braces = 0, brackets = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    switch (text[i]) {
    case '(': ++parens; break; case ')': --parens; break;
    case '{': ++braces; break; case '}': --braces; break;
    case '[': ++brackets; break; case ']': --brackets; break;
    case ',': if (!parens && !braces && !brackets) {
      result.emplace_back(text.substr(begin, i - begin)); begin = i + 1;
    } break;
    default: break;
    }
  }
  result.emplace_back(text.substr(begin));
  return result;
}

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  const auto last = value.find_last_not_of(" \t\r\n");
  return first == std::string::npos ? std::string{} : value.substr(first, last - first + 1);
}

std::string identifier_from_capture(std::string value, bool &reference) {
  value = trim(value);
  const auto open = value.find('(');
  const auto close = value.rfind(')');
  if (open == std::string::npos || close <= open)
    throw std::runtime_error("Recast closure captures must use copy(name) or ref(name)");
  const auto kind = trim(value.substr(0, open));
  if (kind != "copy" && kind != "ref")
    throw std::runtime_error("Recast closure captures must use copy(name) or ref(name)");
  reference = kind == "ref";
  auto name = trim(value.substr(open + 1, close - open - 1));
  if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos ||
      std::isdigit(static_cast<unsigned char>(name.front())))
    throw std::runtime_error("Recast closure capture must name a local object");
  return name;
}

std::string parameters_for_call(std::string parameters) {
  parameters = trim(std::move(parameters));
  if (parameters == "void" || parameters.empty()) return {};
  std::vector<std::string> names;
  for (auto part : comma_parts(parameters)) {
    part = trim(std::move(part));
    std::size_t end = part.find_last_not_of(" \t\r\n*");
    if (end == std::string::npos) throw std::runtime_error("invalid Recast closure parameter");
    std::size_t begin = end;
    while (begin && (std::isalnum(static_cast<unsigned char>(part[begin - 1])) || part[begin - 1] == '_')) --begin;
    if (!std::isalnum(static_cast<unsigned char>(part[end])) && part[end] != '_')
      throw std::runtime_error("Recast closure parameters require names");
    names.push_back(part.substr(begin, end - begin + 1));
  }
  std::string result;
  for (std::size_t i = 0; i < names.size(); ++i) { if (i) result += ", "; result += names[i]; }
  return result;
}

std::string replace_captures(std::string_view body, const std::map<std::string, std::string> &replacements) {
  std::string result;
  for (std::size_t i = 0; i < body.size();) {
    if (std::isalpha(static_cast<unsigned char>(body[i])) || body[i] == '_') {
      const auto begin = i++;
      while (i < body.size() && (std::isalnum(static_cast<unsigned char>(body[i])) || body[i] == '_')) ++i;
      const std::string name(body.substr(begin, i - begin));
      if (const auto found = replacements.find(name); found != replacements.end()) result += found->second;
      else result += name;
    } else {
      result += body[i++];
    }
  }
  return result;
}

} // namespace

std::string transform_closures(const std::string &source) {
  std::vector<Closure> closures;
  for (std::size_t cursor = 0;;) {
    const auto marker = source.find("recast_closure", cursor);
    if (marker == std::string::npos) break;
    auto open = source.find('(', marker + 14);
    if (open == std::string::npos) throw std::runtime_error("malformed recast_closure invocation");
    const auto close = matching(source, open, '(', ')');
    const auto args = comma_parts(std::string_view(source).substr(open + 1, close - open - 1));
    if (args.size() != 5) throw std::runtime_error("recast_closure requires five arguments");
    const auto params_open = args[2].find('('), params_close = args[2].rfind(')');
    const auto captures_open = args[3].find('('), captures_close = args[3].rfind(')');
    const auto body_open = args[4].find('{');
    if (params_open == std::string::npos || params_close <= params_open || captures_open == std::string::npos ||
        captures_close <= captures_open || body_open == std::string::npos)
      throw std::runtime_error("malformed Recast closure parameter, capture, or body list");
    const auto body_close = matching(args[4], body_open, '{', '}');
    Closure closure{};
    closure.name = trim(args[0]);
    closure.result = trim(args[1]);
    closure.parameters = args[2].substr(params_open + 1, params_close - params_open - 1);
    closure.begin = marker;
    closure.end = close + 1;
    closure.id = static_cast<unsigned>(closures.size());
    closure.call_parameters = parameters_for_call(closure.parameters);
    for (auto capture : comma_parts(std::string_view(args[3]).substr(captures_open + 1, captures_close - captures_open - 1))) {
      capture = trim(std::move(capture));
      if (capture.empty()) continue;
      bool reference = false;
      closure.captures.push_back({identifier_from_capture(capture, reference), reference});
    }
    closure.body = args[4].substr(body_open + 1, body_close - body_open - 1);
    closures.push_back(std::move(closure));
    cursor = close + 1;
  }
  if (closures.empty()) return source;
  // Keep the first lowering deliberately local: capture storage is created at
  // the closure declaration and the body is expanded into a GNU statement
  // expression at the call site. This gives C compilers correct lexical
  // capture types without inventing a file-scope type from a block-local
  // declaration; the typed helper lowering remains the next extension point.
  std::string output = source;
  for (auto it = closures.rbegin(); it != closures.rend(); ++it) {
    std::string declarations;
    for (const auto &capture : it->captures) {
      const auto storage = "__recast_" + it->name + "_" + capture.name;
      declarations += capture.reference ? "__typeof__(&" + capture.name + ") " + storage + " = &" + capture.name + ";\n"
                                         : "__typeof__(" + capture.name + ") " + storage + " = " + capture.name + ";\n";
    }
    output.replace(it->begin, it->end - it->begin, declarations);
  }
  for (const auto &closure : closures) {
    std::map<std::string, std::string> names;
    for (const auto &capture : closure.captures) {
      const auto storage = "__recast_" + closure.name + "_" + capture.name;
      names.emplace(capture.name, capture.reference ? "(*" + storage + ")" : storage);
    }
    const auto body = replace_captures(closure.body, names);
    const std::string call = "recast_call(" + closure.name;
    std::size_t pos = 0;
    while ((pos = output.find(call, pos)) != std::string::npos) {
      const auto open = output.find('(', pos + 11);
      const auto close = matching(output, open, '(', ')');
      auto call_parts = comma_parts(std::string_view(output).substr(open + 1, close - open - 1));
      if (call_parts.empty() || trim(call_parts.front()) != closure.name)
        throw std::runtime_error("Recast closure call must name a declared closure");
      call_parts.erase(call_parts.begin());
      std::string expression = "({ ";
      if (!closure.parameters.empty() && closure.parameters != "void") {
        const auto declarations = comma_parts(closure.parameters);
        if (declarations.size() != call_parts.size())
          throw std::runtime_error("Recast closure call argument count does not match its parameters");
        for (std::size_t i = 0; i < declarations.size(); ++i) {
          auto declaration = trim(declarations[i]);
          const auto end = declaration.find_last_not_of(" \t\r\n*");
          std::size_t begin = end;
          while (begin && (std::isalnum(static_cast<unsigned char>(declaration[begin - 1])) || declaration[begin - 1] == '_')) --begin;
          if (end == std::string::npos ||
              (!std::isalnum(static_cast<unsigned char>(declaration[end])) && declaration[end] != '_'))
            throw std::runtime_error("Recast closure parameters require names");
          expression += declaration + " = " + trim(call_parts[i]) + "; ";
        }
      } else if (!call_parts.empty() && !trim(call_parts[0]).empty()) {
        throw std::runtime_error("Recast void closure takes no arguments");
      }
      auto lowered_body = body;
      const auto return_pos = lowered_body.rfind("return");
      if (return_pos != std::string::npos) {
        const auto semicolon = lowered_body.find(';', return_pos);
        if (semicolon == std::string::npos)
          throw std::runtime_error("Recast closure return must end with a semicolon");
        auto value = trim(lowered_body.substr(return_pos + 6, semicolon - return_pos - 6));
        lowered_body.replace(return_pos, semicolon - return_pos + 1, value.empty() ? "(void)0;" : value + ";");
      }
      expression += lowered_body + " })";
      output.replace(pos, close - pos + 1, expression);
      pos += expression.size();
    }
  }
  return output;
  /* The typed file-scope lowering below is retained as design scaffolding
     until declaration-identity type resolution is available. */
#if 0
  std::string generated;
  std::vector<std::pair<std::size_t, std::size_t>> replacements;
  for (const auto &closure : closures) {
    const auto env = "__recast_env_" + std::to_string(closure.id);
    const auto handle = "__recast_handle_" + std::to_string(closure.id);
    const auto fn = "__recast_fn_" + std::to_string(closure.id);
    generated += "struct " + env + " {";
    for (const auto &capture : closure.captures)
      generated += capture.reference ? " __typeof__(&" + capture.name + ") " + capture.name + ";" :
                                       " __typeof__(" + capture.name + ") " + capture.name + ";";
    generated += "};\n";
    std::map<std::string, std::string> names;
    for (const auto &capture : closure.captures)
      names.emplace(capture.name, capture.reference ? "(*__recast_env->" + capture.name + ")" : "__recast_env->" + capture.name);
    generated += "static " + closure.result + " " + fn + "(struct " + env + " *__recast_env";
    if (!closure.parameters.empty() && closure.parameters != "void") generated += ", " + closure.parameters;
    generated += ") " + replace_captures(closure.body, names) + "\n";
    generated += "struct " + handle + " { struct " + env + " ctx; " + closure.result + " (*fn)(struct " + env + " *";
    if (!closure.parameters.empty() && closure.parameters != "void") generated += ", " + closure.parameters;
    generated += "); };\n";
    std::string initializer = "struct " + handle + " " + closure.name + " = {{";
    for (std::size_t i = 0; i < closure.captures.size(); ++i) {
      if (i) initializer += ", ";
      initializer += closure.captures[i].reference ? "&" + closure.captures[i].name : closure.captures[i].name;
    }
    initializer += "}, " + fn + "};";
    replacements.emplace_back(closure.begin, closure.end);
    generated += "\n";
    const auto call = "recast_call(" + closure.name;
    (void)call;
  }
  std::string output = source;
  for (auto it = closures.rbegin(); it != closures.rend(); ++it)
    output.replace(it->begin, it->end - it->begin, "struct __recast_handle_" + std::to_string(it->id) + " " + it->name + " = {{" +
                   [&] { std::string s; for (std::size_t i = 0; i < it->captures.size(); ++i) { if (i) s += ", "; s += it->captures[i].reference ? "&" + it->captures[i].name : it->captures[i].name; } return s; }() +
                   "}, __recast_fn_" + std::to_string(it->id) + "};");
  for (const auto &closure : closures) {
    const std::string call = "recast_call(" + closure.name;
    std::size_t pos = 0;
    while ((pos = output.find(call, pos)) != std::string::npos) {
      const auto open = output.find('(', pos + 11);
      const auto close = matching(output, open, '(', ')');
      const auto arguments = trim(output.substr(open + 1, close - open - 1));
      output.replace(pos, close - pos + 1, closure.name + ".fn(&" + closure.name + ".ctx" + (arguments.empty() ? "" : ", " + arguments) + ")");
      pos += closure.name.size() + 10;
    }
  }
  return generated + output;
#endif
}

} // namespace defer_tool
