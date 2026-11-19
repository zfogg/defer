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
  bool line_comment = false;
  bool block_comment = false;
  bool string = false;
  char quote = 0;
  for (std::size_t i = 0; i < body.size();) {
    const char c = body[i];
    const char next = i + 1 < body.size() ? body[i + 1] : 0;
    if (line_comment) {
      result += c;
      ++i;
      if (c == '\n') line_comment = false;
      continue;
    }
    if (block_comment) {
      result += c;
      ++i;
      if (c == '*' && next == '/') { result += next; ++i; block_comment = false; }
      continue;
    }
    if (string) {
      result += c;
      ++i;
      if (c == '\\' && i < body.size()) { result += body[i++]; continue; }
      if (c == quote) string = false;
      continue;
    }
    if (c == '/' && next == '/') { result += "//"; i += 2; line_comment = true; continue; }
    if (c == '/' && next == '*') { result += "/*"; i += 2; block_comment = true; continue; }
    if (c == '"' || c == '\'') { result += c; ++i; string = true; quote = c; continue; }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
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

unsigned brace_depth_before(std::string_view source, std::size_t end) {
  unsigned depth = 0;
  bool line_comment = false, block_comment = false, string = false;
  char quote = 0;
  for (std::size_t i = 0; i < end; ++i) {
    const char c = source[i];
    const char next = i + 1 < end ? source[i + 1] : 0;
    if (line_comment) { if (c == '\n') line_comment = false; continue; }
    if (block_comment) { if (c == '*' && next == '/') { block_comment = false; ++i; } continue; }
    if (string) { if (c == '\\') ++i; else if (c == quote) string = false; continue; }
    if (c == '/' && next == '/') { line_comment = true; ++i; continue; }
    if (c == '/' && next == '*') { block_comment = true; ++i; continue; }
    if (c == '"' || c == '\'') { string = true; quote = c; continue; }
    if (c == '{') ++depth;
    else if (c == '}' && depth) --depth;
  }
  return depth;
}

} // namespace

std::string transform_closures(const std::string &source) {
  std::vector<Closure> closures;
  for (std::size_t cursor = 0;;) {
    auto marker = source.find("closure", cursor);
    if (marker == std::string::npos) break;
    const bool namespaced = marker >= 7 && source.compare(marker - 7, 7, "recast_") == 0;
    if (namespaced) marker -= 7;
    auto invocation = marker + (namespaced ? 14 : 7);
    while (invocation < source.size() && std::isspace(static_cast<unsigned char>(source[invocation]))) ++invocation;
    if (invocation >= source.size() || source[invocation] != '(') {
      cursor = marker + (namespaced ? 14 : 7);
      continue;
    }
    const auto line_start = source.rfind('\n', marker);
    if (source.substr(line_start == std::string::npos ? 0 : line_start + 1,
                      marker - (line_start == std::string::npos ? 0 : line_start + 1)).find("#define") != std::string::npos) {
      cursor = marker + 7;
      continue;
    }
    auto open = invocation;
    if (open == std::string::npos) throw std::runtime_error("malformed closure invocation");
    const auto close = matching(source, open, '(', ')');
    const auto args = comma_parts(std::string_view(source).substr(open + 1, close - open - 1));
    if (args.size() != 5) throw std::runtime_error("closure requires five arguments");
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
  // Materialize captures where the closure is declared.  This keeps copy()
  // captures independent of later writes and makes ref() captures point at
  // the original object.  Clang-cl accepts __typeof__ in MS compatibility
  // mode, while the generated call itself remains a normal C expression.
  std::string output = source;
  for (auto it = closures.rbegin(); it != closures.rend(); ++it) {
    std::string declarations;
    for (const auto &capture : it->captures) {
      const auto storage = "__recast_" + it->name + "_" + capture.name;
      if (capture.reference) {
        declarations += "__typeof__(&" + capture.name + ") " + storage + " = &" + capture.name + ";\n";
      } else {
        declarations += "__typeof__(" + capture.name + ") " + storage + " = " + capture.name + ";\n";
      }
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
    const std::string call = closure.name + "(";
    std::size_t pos = 0;
    while ((pos = output.find(call, pos)) != std::string::npos) {
      if (brace_depth_before(output, pos) == 0)
        throw std::runtime_error("Recast closure calls are supported only inside function bodies");
      const auto open = output.find('(', pos + closure.name.size());
      const auto close = matching(output, open, '(', ')');
      auto call_parts = comma_parts(std::string_view(output).substr(open + 1, close - open - 1));
      // A closure call is an expression, so keep the compound statement
      // wrapped as a GNU C statement expression.  The previous bare block
      // produced `return { ... } + value`, which libclang rejects before the
      // transformed source can be validated.
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
      std::string result_expression;
      const auto return_pos = lowered_body.rfind("return");
      if (return_pos != std::string::npos) {
        const auto semicolon = lowered_body.find(';', return_pos);
        if (semicolon == std::string::npos)
          throw std::runtime_error("Recast closure return must end with a semicolon");
        auto value = trim(lowered_body.substr(return_pos + 6, semicolon - return_pos - 6));
        if (value.empty())
          lowered_body.replace(return_pos, semicolon - return_pos + 1, "(void)0;");
        else {
          result_expression = "__recast_result_" + closure.name;
          lowered_body.replace(return_pos, semicolon - return_pos + 1, result_expression + " = " + value + ";");
        }
      }
      if (!result_expression.empty())
        expression += closure.result + " " + result_expression + " = (" + closure.result + "){0}; ";
      expression += lowered_body;
      if (!result_expression.empty()) expression += " " + result_expression + ";";
      expression += " })";
      output.replace(pos, close - pos + 1, expression);
      pos += expression.size();
    }
  }
  return output;
}

} // namespace defer_tool
