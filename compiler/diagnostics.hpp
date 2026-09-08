#ifndef PUNPUN_DIAGNOSTICS_HPP
#define PUNPUN_DIAGNOSTICS_HPP

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ppdiag {
namespace fs = std::filesystem;

enum class Severity { Error, Warning, Note };

inline const char *severity_name(Severity severity) {
    switch (severity) {
        case Severity::Error: return "error";
        case Severity::Warning: return "warning";
        case Severity::Note: return "note";
    }
    return "error";
}

inline std::string read_source_line(const fs::path &file, int wanted_line) {
    if (wanted_line < 1) return {};
    std::ifstream input(file);
    if (!input) return {};
    std::string line;
    for (int current = 1; current <= wanted_line; ++current) {
        if (!std::getline(input, line)) return {};
    }
    return line;
}

inline std::string format(const fs::path &file,
                          int line,
                          int column,
                          size_t highlight_length,
                          const std::string &code,
                          const std::string &message,
                          const std::string &label = {},
                          const std::string &help = {},
                          Severity severity = Severity::Error,
                          const std::string &fix_replacement = {}) {
    std::ostringstream out;
    out << severity_name(severity);
    if (!code.empty()) out << '[' << code << ']';
    out << ": " << message << '\n';
    out << "  --> " << file.string() << ':' << line << ':' << column << '\n';

    const std::string source_line = read_source_line(file, line);
    if (!source_line.empty()) {
        const std::string line_number = std::to_string(line);
        out << std::string(line_number.size() + 1, ' ') << "|\n";
        out << ' ' << line_number << " | " << source_line << '\n';
        out << std::string(line_number.size() + 1, ' ') << "| ";
        const int safe_column = std::max(column, 1);
        for (int i = 1; i < safe_column; ++i) {
            const size_t source_index = static_cast<size_t>(i - 1);
            out << (source_index < source_line.size() && source_line[source_index] == '\t' ? '\t' : ' ');
        }
        const size_t available = safe_column <= static_cast<int>(source_line.size())
            ? source_line.size() - static_cast<size_t>(safe_column - 1)
            : 1;
        const size_t marker_length = std::max<size_t>(1, std::min(highlight_length, std::max<size_t>(1, available)));
        out << std::string(marker_length, '^');
        if (!label.empty()) out << ' ' << label;
        out << '\n';
    }
    if (!help.empty()) out << "   = help: " << help << '\n';
    if (!fix_replacement.empty()) {
        out << "   = fix-it: " << file.string() << ':' << line << ':' << column
            << ':' << std::max<size_t>(1, highlight_length) << " => ";
        for (char c : fix_replacement) {
            if (c == '\\' || c == '"') out << '\\';
            if (c == '\n') out << "\\n";
            else out << c;
        }
        out << " [machine-applicable]\n";
    }
    return out.str();
}

}  // namespace ppdiag

#endif  // PUNPUN_DIAGNOSTICS_HPP
