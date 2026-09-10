// ppc — the PunPun compiler.
// Core vocabulary types shared by every layer of the pipeline.
#ifndef PPC_SUPPORT_COMMON_HPP
#define PPC_SUPPORT_COMMON_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ppc {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

/// Identifies a file inside a SourceManager. 0 is a valid id; `invalid()`
/// is used for synthetic nodes that have no source text (prelude, desugaring).
struct FileId {
    u32 value = 0xFFFFFFFFu;
    static FileId invalid() { return FileId{0xFFFFFFFFu}; }
    bool valid() const { return value != 0xFFFFFFFFu; }
    bool operator==(const FileId &o) const { return value == o.value; }
    bool operator!=(const FileId &o) const { return value != o.value; }
};

/// A half-open byte range [start, end) inside one file.
///
/// Spans are byte offsets rather than line/column pairs because the lexer
/// produces millions of them and converting to line/column is only needed when
/// a diagnostic is actually rendered. SourceManager does that conversion with a
/// binary search over a precomputed line table.
struct Span {
    FileId file = FileId::invalid();
    u32 start = 0;
    u32 end = 0;

    Span() = default;
    Span(FileId f, u32 s, u32 e) : file(f), start(s), end(e) {}

    bool valid() const { return file.valid(); }
    u32 length() const { return end > start ? end - start : 0; }

    /// Smallest span covering both operands. Spans in different files do not
    /// merge; the left operand wins so diagnostics stay anchored somewhere real.
    Span merge(const Span &other) const {
        if (!valid()) return other;
        if (!other.valid() || other.file != file) return *this;
        return Span{file, start < other.start ? start : other.start,
                    end > other.end ? end : other.end};
    }
};

/// Interned string handle. Comparing two Symbols is an integer compare, which
/// matters because name resolution compares identifiers constantly.
struct Symbol {
    u32 index = 0xFFFFFFFFu;
    bool valid() const { return index != 0xFFFFFFFFu; }
    bool operator==(const Symbol &o) const { return index == o.index; }
    bool operator!=(const Symbol &o) const { return index != o.index; }
    bool operator<(const Symbol &o) const { return index < o.index; }
};

struct SymbolHash {
    std::size_t operator()(const Symbol &s) const noexcept {
        return std::hash<u32>{}(s.index);
    }
};

/// Global-ish string interner. One instance lives on the CompilerSession and is
/// threaded through every phase; there is no hidden singleton so tests can spin
/// up independent sessions.
class Interner {
  public:
    Interner() { intern(""); }

    Symbol intern(std::string_view text) {
        auto it = map_.find(std::string(text));
        if (it != map_.end()) return Symbol{it->second};
        const u32 index = static_cast<u32>(storage_.size());
        storage_.emplace_back(text);
        map_.emplace(storage_.back(), index);
        return Symbol{index};
    }

    const std::string &text(Symbol symbol) const {
        static const std::string empty;
        if (!symbol.valid() || symbol.index >= storage_.size()) return empty;
        return storage_[symbol.index];
    }

    std::size_t size() const { return storage_.size(); }

  private:
    std::vector<std::string> storage_;
    std::unordered_map<std::string, u32> map_;
};

/// Concatenate with no separator. Used constantly when building diagnostic
/// messages and mangled symbol names.
inline std::string cat(std::initializer_list<std::string_view> parts) {
    std::size_t total = 0;
    for (auto part : parts) total += part.size();
    std::string result;
    result.reserve(total);
    for (auto part : parts) result.append(part);
    return result;
}

}  // namespace ppc

#endif
