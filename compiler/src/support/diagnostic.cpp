#include "ppc/support/diagnostic.hpp"

#include <algorithm>
#include <sstream>

namespace ppc {

namespace {

// ANSI colors, only emitted when the driver says the stream is a terminal.
constexpr const char *kReset = "\033[0m";
constexpr const char *kBold = "\033[1m";
constexpr const char *kRed = "\033[31;1m";
constexpr const char *kYellow = "\033[33;1m";
constexpr const char *kCyan = "\033[36;1m";
constexpr const char *kBlue = "\033[34;1m";

std::string json_escape(const std::string &text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Tabs would misalign the caret row, so they are expanded to a single space in
// the excerpt and counted as one column.
std::string expand_tabs(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) out += (c == '\t') ? ' ' : c;
    return out;
}

}  // namespace

const char *severity_name(Severity severity) {
    switch (severity) {
        case Severity::Note: return "note";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
        case Severity::Fatal: return "fatal";
    }
    return "error";
}

std::string code_string(Code code) {
    char buffer[8];
    const unsigned value = static_cast<unsigned>(code);
    // Codes from 2000 up are warnings and carry a W so the two ranges cannot be
    // confused when a code is quoted on its own.
    std::snprintf(buffer, sizeof(buffer), "%c%04u", value >= 2000 ? 'W' : 'E', value);
    return buffer;
}

Diagnostic &Diagnostic::label(Span span, std::string text) {
    labels.push_back(Label{span, std::move(text), true});
    return *this;
}

Diagnostic &Diagnostic::secondary(Span span, std::string text) {
    labels.push_back(Label{span, std::move(text), false});
    return *this;
}

Diagnostic &Diagnostic::note(std::string text) {
    notes.push_back(std::move(text));
    return *this;
}

Diagnostic &Diagnostic::with_help(std::string text) {
    help = std::move(text);
    return *this;
}

Diagnostic &DiagnosticEngine::report(Severity severity, Code code, std::string message) {
    diagnostics_.push_back(Diagnostic{severity, code, std::move(message), {}, {}, {}});
    if (severity == Severity::Error || severity == Severity::Fatal) ++error_count_;
    if (severity == Severity::Warning) ++warning_count_;
    return diagnostics_.back();
}

Diagnostic &DiagnosticEngine::error(Code code, std::string message) {
    return report(Severity::Error, code, std::move(message));
}

Diagnostic &DiagnosticEngine::warning(Code code, std::string message) {
    return report(Severity::Warning, code, std::move(message));
}

void DiagnosticEngine::rewind(std::size_t position) {
    if (position >= diagnostics_.size()) return;
    // The severity counters have to be rolled back too, or a discarded error
    // would keep `has_errors()` true and abort a compilation that is fine.
    for (std::size_t i = position; i < diagnostics_.size(); ++i) {
        switch (diagnostics_[i].severity) {
            case Severity::Error:
            case Severity::Fatal:
                if (error_count_) --error_count_;
                break;
            case Severity::Warning:
                if (warning_count_) --warning_count_;
                break;
            case Severity::Note:
                break;
        }
    }
    diagnostics_.resize(position);
}

void DiagnosticEngine::clear() {
    diagnostics_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

std::string DiagnosticEngine::render_one(const Diagnostic &diagnostic) const {
    const char *severity_color = kRed;
    switch (diagnostic.severity) {
        case Severity::Note: severity_color = kCyan; break;
        case Severity::Warning: severity_color = kYellow; break;
        case Severity::Error:
        case Severity::Fatal: severity_color = kRed; break;
    }
    const char *reset = color_ ? kReset : "";
    const char *bold = color_ ? kBold : "";
    const char *blue = color_ ? kBlue : "";
    const char *sev = color_ ? severity_color : "";

    std::ostringstream out;
    out << sev << severity_name(diagnostic.severity) << "[" << code_string(diagnostic.code)
        << "]" << reset << ": " << bold << diagnostic.message << reset << "\n";

    // Primary label first; it decides the file header line.
    std::vector<const Label *> ordered;
    for (const Label &l : diagnostic.labels) {
        if (l.primary) ordered.push_back(&l);
    }
    for (const Label &l : diagnostic.labels) {
        if (!l.primary) ordered.push_back(&l);
    }

    // Gutter width is driven by the largest line number in the diagnostic so
    // multi-label output stays aligned.
    u32 widest = 1;
    for (const Label *l : ordered) {
        if (!l->span.valid() || !sources_.has(l->span.file)) continue;
        widest = std::max(widest, sources_.locate(l->span.file, l->span.start).line);
    }
    const std::size_t gutter = std::to_string(widest).size();
    const std::string pad(gutter, ' ');

    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const Label &l = *ordered[i];
        if (!l.span.valid() || !sources_.has(l.span.file)) continue;

        const LineColumn lc = sources_.locate(l.span.file, l.span.start);
        const std::string excerpt = expand_tabs(sources_.line_text(l.span.file, l.span.start));

        out << blue << pad << "--> " << reset << sources_.file(l.span.file).path << ":"
            << lc.line << ":" << lc.column << "\n";
        out << blue << pad << " |" << reset << "\n";

        std::string number = std::to_string(lc.line);
        number.insert(number.begin(), gutter - number.size(), ' ');
        out << blue << number << " |" << reset << " " << excerpt << "\n";

        // The underline stops at end-of-line so a multi-line span does not draw
        // carets past the excerpt it is annotating.
        const u32 column = lc.column > 0 ? lc.column - 1 : 0;
        u32 width = l.span.length() ? l.span.length() : 1;
        if (column + width > excerpt.size()) {
            width = static_cast<u32>(excerpt.size() > column ? excerpt.size() - column : 1);
        }
        if (width == 0) width = 1;

        out << blue << pad << " |" << reset << " " << std::string(column, ' ');
        out << (l.primary ? sev : blue) << std::string(width, l.primary ? '^' : '-');
        if (!l.message.empty()) out << " " << l.message;
        out << reset << "\n";
        if (i + 1 < ordered.size()) out << blue << pad << " |" << reset << "\n";
    }

    for (const std::string &note : diagnostic.notes) {
        out << blue << pad << " = " << reset << (color_ ? kCyan : "") << "note" << reset
            << ": " << note << "\n";
    }
    if (!diagnostic.help.empty()) {
        out << blue << pad << " = " << reset << (color_ ? kCyan : "") << "help" << reset
            << ": " << diagnostic.help << "\n";
    }
    return out.str();
}

std::string DiagnosticEngine::render() const {
    std::ostringstream out;
    for (const Diagnostic &diagnostic : diagnostics_) {
        out << render_one(diagnostic) << "\n";
    }
    if (error_count_ > 0) {
        out << (color_ ? kRed : "") << "error" << (color_ ? kReset : "") << ": aborting due to "
            << error_count_ << (error_count_ == 1 ? " error" : " errors");
        if (warning_count_ > 0) {
            out << " (" << warning_count_ << (warning_count_ == 1 ? " warning" : " warnings")
                << " emitted)";
        }
        out << "\n";
    } else if (warning_count_ > 0) {
        out << (color_ ? kYellow : "") << "warning" << (color_ ? kReset : "") << ": "
            << warning_count_ << (warning_count_ == 1 ? " warning" : " warnings") << " emitted\n";
    }
    return out.str();
}

std::string DiagnosticEngine::render_json() const {
    std::ostringstream out;
    out << "[";
    bool first = true;
    for (const Diagnostic &diagnostic : diagnostics_) {
        if (!first) out << ",";
        first = false;
        out << "\n  {";
        out << "\"severity\":\"" << severity_name(diagnostic.severity) << "\"";
        out << ",\"code\":\"" << code_string(diagnostic.code) << "\"";
        out << ",\"message\":\"" << json_escape(diagnostic.message) << "\"";
        out << ",\"labels\":[";
        bool first_label = true;
        for (const Label &l : diagnostic.labels) {
            if (!l.span.valid() || !sources_.has(l.span.file)) continue;
            if (!first_label) out << ",";
            first_label = false;
            const LineColumn lc = sources_.locate(l.span.file, l.span.start);
            const LineColumn end = sources_.locate(l.span.file, l.span.end);
            out << "{\"file\":\"" << json_escape(sources_.file(l.span.file).path) << "\"";
            out << ",\"line\":" << lc.line << ",\"column\":" << lc.column;
            out << ",\"end_line\":" << end.line << ",\"end_column\":" << end.column;
            out << ",\"primary\":" << (l.primary ? "true" : "false");
            out << ",\"message\":\"" << json_escape(l.message) << "\"}";
        }
        out << "]";
        out << ",\"notes\":[";
        for (std::size_t i = 0; i < diagnostic.notes.size(); ++i) {
            if (i) out << ",";
            out << "\"" << json_escape(diagnostic.notes[i]) << "\"";
        }
        out << "]";
        out << ",\"help\":\"" << json_escape(diagnostic.help) << "\"";
        out << "}";
    }
    out << "\n]\n";
    return out.str();
}

}  // namespace ppc
