#ifndef PPC_SUPPORT_SOURCE_HPP
#define PPC_SUPPORT_SOURCE_HPP

#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

#include "ppc/support/common.hpp"

namespace ppc {

struct LineColumn {
    u32 line = 1;    // 1-based
    u32 column = 1;  // 1-based, counted in bytes
};

/// One loaded source file plus a precomputed table of line-start offsets.
struct SourceFile {
    FileId id;
    std::string path;      // as written by the user, for diagnostics
    std::string abs_path;  // normalized, for the module cache and dedup
    std::string text;
    std::vector<u32> line_starts;
};

/// Owns every byte of source the compiler has read.
///
/// Files are loaded at most once; asking for the same absolute path twice
/// returns the original FileId. That property is what makes the module loader's
/// cycle detection cheap.
class SourceManager {
  public:
    /// Reads a file from disk. Returns nullopt when the file cannot be opened.
    std::optional<FileId> load(const std::string &path);

    /// Registers text that did not come from disk (the prelude, `--expr`
    /// snippets, test fixtures). `name` shows up in diagnostics.
    FileId add_virtual(const std::string &name, std::string text);

    /// Supplies in-memory contents for a real path.
    ///
    /// `load` returns this instead of reading the file, which is how an editor
    /// gets diagnostics for a buffer it has not saved. Overlays are consulted
    /// by absolute path, so it does not matter how the importer spelled it.
    void add_overlay(const std::string &path, std::string text);
    void remove_overlay(const std::string &path);

    const SourceFile &file(FileId id) const { return files_[id.value]; }
    bool has(FileId id) const { return id.valid() && id.value < files_.size(); }
    std::size_t count() const { return files_.size(); }

    /// Byte offset -> 1-based line/column. Binary search over line_starts.
    LineColumn locate(FileId id, u32 offset) const;

    /// The full text of the line containing `offset`, without its newline.
    std::string_view line_text(FileId id, u32 offset) const;

    /// Convenience for rendering: "path:line:column".
    std::string describe(const Span &span) const;

    /// Already-loaded file whose absolute path matches, if any.
    std::optional<FileId> find_by_abs_path(const std::string &abs) const;

  private:
    static void index_lines(SourceFile &file);

    std::vector<SourceFile> files_;
    /// Absolute path -> unsaved contents.
    std::unordered_map<std::string, std::string> overlays_;
};

/// Resolves a path to an absolute, lexically normalized form. Does not require
/// the file to exist, so it is safe to call on candidate module paths.
std::string normalize_path(const std::string &path);

/// Directory portion of a path, or "." when there is none.
std::string parent_directory(const std::string &path);

/// Joins two path fragments with the platform separator, collapsing duplicates.
std::string join_path(const std::string &left, const std::string &right);

/// Filename without its directory or final extension.
std::string path_stem(const std::string &path);

bool file_exists(const std::string &path);
/// Size in bytes, or -1 when the path is not a readable regular file. Used to
/// spot a truncated cache entry left by an interrupted build.
i64 file_size(const std::string &path);
bool read_file(const std::string &path, std::string &out);
bool write_file(const std::string &path, const std::string &text);

}  // namespace ppc

#endif
