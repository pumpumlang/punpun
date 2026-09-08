#ifndef PUNPUN_FORMATTER_HPP
#define PUNPUN_FORMATTER_HPP

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>

namespace ppfmt {

inline std::string trim(const std::string &line) {
    std::size_t first = 0;
    while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first]))) ++first;
    std::size_t last = line.size();
    while (last > first && std::isspace(static_cast<unsigned char>(line[last - 1]))) --last;
    return line.substr(first, last - first);
}

inline bool starts_word(const std::string &line, const std::string &word) {
    if (line.size() < word.size() || line.compare(0, word.size(), word) != 0) return false;
    return line.size() == word.size() || !std::isalnum(static_cast<unsigned char>(line[word.size()]));
}

inline bool opens_legacy_block(const std::string &line) {
    if (line.empty() || line.back() != ':') return false;
    return starts_word(line, "launch") || starts_word(line, "craft") || starts_word(line, "shape") ||
           starts_word(line, "when") || starts_word(line, "otherwise") || starts_word(line, "whilst") ||
           starts_word(line, "each");
}

// Count braces outside ordinary quoted strings and line comments. Triple-quoted
// foreign/source blocks are handled at the line loop level and left untouched.
inline std::pair<int, int> brace_counts(const std::string &line) {
    int opens = 0, closes = 0;
    bool string = false, escape = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (!string && c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (!string && c == '#') break;
        if (string) {
            if (escape) { escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == '"') string = false;
            continue;
        }
        if (c == '"') { string = true; continue; }
        if (c == '{') ++opens;
        else if (c == '}') ++closes;
    }
    return {opens, closes};
}

inline int leading_closing_braces(const std::string &line) {
    int count = 0;
    for (char c : line) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '}') { ++count; continue; }
        break;
    }
    return count;
}

inline bool toggles_triple(const std::string &line) {
    std::size_t count = 0, at = 0;
    while ((at = line.find("\"\"\"", at)) != std::string::npos) { ++count; at += 3; }
    return (count % 2) != 0;
}

inline std::string format_source(const std::string &source) {
    std::istringstream input(source);
    std::ostringstream output;
    std::string raw;
    int indent = 0;
    bool first_line = true;
    bool triple = false;

    while (std::getline(input, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (!first_line) output << '\n';
        first_line = false;

        if (triple) {
            // Injected/embedded source is developer-owned source code. PunPun's
            // formatter must not rewrite its indentation or braces.
            output << raw;
            if (toggles_triple(raw)) triple = false;
            continue;
        }

        const std::string line = trim(raw);
        if (line.empty()) continue;

        const bool starts_triple = toggles_triple(line);
        const bool legacy_close = starts_word(line, "done") || starts_word(line, "otherwise");
        const int leading_closes = leading_closing_braces(line);
        if (legacy_close) indent = std::max(0, indent - 1);
        if (leading_closes) indent = std::max(0, indent - leading_closes);

        output << std::string(static_cast<std::size_t>(indent) * 4, ' ') << line;

        if (starts_triple) { triple = true; continue; }
        const auto [opens, closes] = brace_counts(line);
        indent += opens - (closes - leading_closes);
        if (opens_legacy_block(line)) ++indent;
        indent = std::max(0, indent);
    }

    output << '\n';
    return output.str();
}

}  // namespace ppfmt
#endif
