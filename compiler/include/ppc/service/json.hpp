#ifndef PPC_SERVICE_JSON_HPP
#define PPC_SERVICE_JSON_HPP

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ppc {
namespace json {

/// A minimal JSON value.
///
/// PPC has no external dependencies and that is worth keeping, so LSP's JSON
/// needs is met here rather than by pulling in a library. The scope is
/// deliberately small: parse what a language client sends, emit what it
/// expects, and nothing more. It is not a general-purpose JSON library and
/// should not grow into one.
///
/// Objects preserve insertion order via std::map on key, which is enough for a
/// protocol where field order is not significant.
class Value {
  public:
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool value) : kind_(Kind::Bool), boolean_(value) {}
    Value(double value) : kind_(Kind::Number), number_(value) {}
    Value(int value) : kind_(Kind::Number), number_(value) {}
    Value(long long value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
    Value(const char *value) : kind_(Kind::String), string_(value) {}
    Value(std::string value) : kind_(Kind::String), string_(std::move(value)) {}

    static Value array() {
        Value value;
        value.kind_ = Kind::Array;
        return value;
    }
    static Value object() {
        Value value;
        value.kind_ = Kind::Object;
        return value;
    }

    Kind kind() const { return kind_; }
    bool is_null() const { return kind_ == Kind::Null; }
    bool is_object() const { return kind_ == Kind::Object; }
    bool is_array() const { return kind_ == Kind::Array; }
    bool is_string() const { return kind_ == Kind::String; }
    bool is_number() const { return kind_ == Kind::Number; }

    bool as_bool(bool fallback = false) const {
        return kind_ == Kind::Bool ? boolean_ : fallback;
    }
    double as_number(double fallback = 0.0) const {
        return kind_ == Kind::Number ? number_ : fallback;
    }
    int as_int(int fallback = 0) const {
        return kind_ == Kind::Number ? static_cast<int>(number_) : fallback;
    }
    const std::string &as_string() const {
        static const std::string empty;
        return kind_ == Kind::String ? string_ : empty;
    }

    /// Member access. Returns a null Value when absent, so a chain of lookups
    /// on a malformed message degrades to null instead of crashing.
    const Value &operator[](const std::string &key) const {
        static const Value null_value;
        if (kind_ != Kind::Object) return null_value;
        auto it = members_.find(key);
        return it == members_.end() ? null_value : it->second;
    }

    const std::vector<Value> &elements() const { return elements_; }
    const std::map<std::string, Value> &members() const { return members_; }

    void set(const std::string &key, Value value) {
        kind_ = Kind::Object;
        members_[key] = std::move(value);
    }
    void push(Value value) {
        kind_ = Kind::Array;
        elements_.push_back(std::move(value));
    }

    bool has(const std::string &key) const {
        return kind_ == Kind::Object && members_.count(key) != 0;
    }

    /// Compact serialization. No pretty-printing: the consumer is a program.
    std::string dump() const;

  private:
    Kind kind_ = Kind::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> elements_;
    std::map<std::string, Value> members_;
};

/// Parses `text`. On malformed input returns a null Value and sets `error`;
/// it never throws, because a language client is entitled to send garbage and
/// the server must stay up.
Value parse(const std::string &text, std::string &error);

}  // namespace json
}  // namespace ppc

#endif
