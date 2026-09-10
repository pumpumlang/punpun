#include "ppc/service/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ppc {
namespace json {

namespace {

void escape_into(std::string &out, const std::string &text) {
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    // Bytes >= 0x80 are passed through unchanged: the input is
                    // UTF-8 and JSON strings are UTF-8, so escaping them would
                    // only make the output larger and harder to read.
                    out += static_cast<char>(c);
                }
        }
    }
}

struct Parser {
    const std::string &text;
    std::size_t position = 0;
    std::string error;

    void skip_space() {
        while (position < text.size() &&
               (text[position] == ' ' || text[position] == '\t' || text[position] == '\n' ||
                text[position] == '\r')) {
            ++position;
        }
    }

    bool consume(char c) {
        skip_space();
        if (position < text.size() && text[position] == c) {
            ++position;
            return true;
        }
        return false;
    }

    bool fail(const std::string &message) {
        if (error.empty()) {
            error = message + " at offset " + std::to_string(position);
        }
        return false;
    }

    bool parse_string(std::string &out) {
        if (!consume('"')) return fail("expected a string");
        while (position < text.size()) {
            const char c = text[position++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (position >= text.size()) break;
            const char escaped = text[position++];
            switch (escaped) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (position + 4 > text.size()) return fail("truncated \\u escape");
                    const std::string hex = text.substr(position, 4);
                    position += 4;
                    unsigned code = std::strtoul(hex.c_str(), nullptr, 16);
                    // Surrogate pairs are decoded so a client sending an
                    // astral-plane character in an identifier round-trips.
                    if (code >= 0xD800 && code <= 0xDBFF && position + 6 <= text.size() &&
                        text[position] == '\\' && text[position + 1] == 'u') {
                        const unsigned low =
                            std::strtoul(text.substr(position + 2, 4).c_str(), nullptr, 16);
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                            position += 6;
                        }
                    }
                    // Encode as UTF-8.
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code < 0x10000) {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xF0 | (code >> 18));
                        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_value(Value &out) {
        skip_space();
        if (position >= text.size()) return fail("unexpected end of input");

        const char c = text[position];
        if (c == '{') {
            ++position;
            out = Value::object();
            skip_space();
            if (consume('}')) return true;
            while (true) {
                std::string key;
                if (!parse_string(key)) return false;
                if (!consume(':')) return fail("expected ':'");
                Value member;
                if (!parse_value(member)) return false;
                out.set(key, std::move(member));
                if (consume(',')) continue;
                if (consume('}')) return true;
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++position;
            out = Value::array();
            skip_space();
            if (consume(']')) return true;
            while (true) {
                Value element;
                if (!parse_value(element)) return false;
                out.push(std::move(element));
                if (consume(',')) continue;
                if (consume(']')) return true;
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string value;
            if (!parse_string(value)) return false;
            out = Value(std::move(value));
            return true;
        }
        if (text.compare(position, 4, "true") == 0) {
            position += 4;
            out = Value(true);
            return true;
        }
        if (text.compare(position, 5, "false") == 0) {
            position += 5;
            out = Value(false);
            return true;
        }
        if (text.compare(position, 4, "null") == 0) {
            position += 4;
            out = Value();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char *stop = nullptr;
            const double number = std::strtod(text.c_str() + position, &stop);
            if (stop == text.c_str() + position) return fail("malformed number");
            position = static_cast<std::size_t>(stop - text.c_str());
            out = Value(number);
            return true;
        }
        return fail("unexpected character");
    }
};

}  // namespace

std::string Value::dump() const {
    switch (kind_) {
        case Kind::Null: return "null";
        case Kind::Bool: return boolean_ ? "true" : "false";
        case Kind::Number: {
            // Integral values print without a decimal point, because LSP
            // identifiers and positions are integers and a client may be strict.
            if (number_ == std::floor(number_) && std::fabs(number_) < 1e15) {
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%lld",
                              static_cast<long long>(number_));
                return buffer;
            }
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.17g", number_);
            return buffer;
        }
        case Kind::String: {
            std::string out = "\"";
            escape_into(out, string_);
            out += "\"";
            return out;
        }
        case Kind::Array: {
            std::string out = "[";
            for (std::size_t i = 0; i < elements_.size(); ++i) {
                if (i) out += ",";
                out += elements_[i].dump();
            }
            out += "]";
            return out;
        }
        case Kind::Object: {
            std::string out = "{";
            bool first = true;
            for (const auto &entry : members_) {
                if (!first) out += ",";
                first = false;
                out += "\"";
                escape_into(out, entry.first);
                out += "\":";
                out += entry.second.dump();
            }
            out += "}";
            return out;
        }
    }
    return "null";
}

Value parse(const std::string &text, std::string &error) {
    Parser parser{text, 0, {}};
    Value value;
    if (!parser.parse_value(value)) {
        error = parser.error;
        return Value();
    }
    error.clear();
    return value;
}

}  // namespace json
}  // namespace ppc
