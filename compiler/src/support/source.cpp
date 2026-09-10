#include "ppc/support/source.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace ppc {

namespace {
constexpr char kSeparator = '/';
}

void SourceManager::add_overlay(const std::string &path, std::string text) {
    overlays_[normalize_path(path)] = std::move(text);
}

void SourceManager::remove_overlay(const std::string &path) {
    overlays_.erase(normalize_path(path));
}

std::optional<FileId> SourceManager::load(const std::string &path) {
    const std::string abs = normalize_path(path);
    if (auto existing = find_by_abs_path(abs)) return existing;

    std::string text;
    // An overlay wins over the file on disk: an editor's unsaved buffer is the
    // version the user is actually looking at, and diagnostics must describe
    // that, not a stale copy.
    auto overlay = overlays_.find(abs);
    if (overlay != overlays_.end()) {
        text = overlay->second;
    } else if (!read_file(path, text)) {
        return std::nullopt;
    }

    SourceFile file;
    file.id = FileId{static_cast<u32>(files_.size())};
    file.path = path;
    file.abs_path = abs;
    file.text = std::move(text);
    index_lines(file);
    files_.push_back(std::move(file));
    return files_.back().id;
}

FileId SourceManager::add_virtual(const std::string &name, std::string text) {
    SourceFile file;
    file.id = FileId{static_cast<u32>(files_.size())};
    file.path = name;
    // Virtual files get a marker prefix so they can never collide with a real
    // path in find_by_abs_path.
    file.abs_path = "<virtual>:" + name;
    file.text = std::move(text);
    index_lines(file);
    files_.push_back(std::move(file));
    return files_.back().id;
}

void SourceManager::index_lines(SourceFile &file) {
    file.line_starts.clear();
    file.line_starts.push_back(0);
    for (u32 i = 0; i < file.text.size(); ++i) {
        if (file.text[i] == '\n') file.line_starts.push_back(i + 1);
    }
}

LineColumn SourceManager::locate(FileId id, u32 offset) const {
    if (!has(id)) return {};
    const SourceFile &f = files_[id.value];
    if (offset > f.text.size()) offset = static_cast<u32>(f.text.size());
    // upper_bound gives the first line start strictly after `offset`; the line
    // containing offset is the one before it.
    auto it = std::upper_bound(f.line_starts.begin(), f.line_starts.end(), offset);
    const std::size_t index = static_cast<std::size_t>(it - f.line_starts.begin()) - 1;
    LineColumn result;
    result.line = static_cast<u32>(index + 1);
    result.column = offset - f.line_starts[index] + 1;
    return result;
}

std::string_view SourceManager::line_text(FileId id, u32 offset) const {
    if (!has(id)) return {};
    const SourceFile &f = files_[id.value];
    if (offset > f.text.size()) offset = static_cast<u32>(f.text.size());
    auto it = std::upper_bound(f.line_starts.begin(), f.line_starts.end(), offset);
    const std::size_t index = static_cast<std::size_t>(it - f.line_starts.begin()) - 1;
    const u32 begin = f.line_starts[index];
    u32 end = begin;
    while (end < f.text.size() && f.text[end] != '\n') ++end;
    // Tolerate CRLF so Windows-authored sources render without a stray caret.
    if (end > begin && f.text[end - 1] == '\r') --end;
    return std::string_view(f.text).substr(begin, end - begin);
}

std::string SourceManager::describe(const Span &span) const {
    if (!span.valid() || !has(span.file)) return "<unknown>";
    const LineColumn lc = locate(span.file, span.start);
    return files_[span.file.value].path + ":" + std::to_string(lc.line) + ":" +
           std::to_string(lc.column);
}

std::optional<FileId> SourceManager::find_by_abs_path(const std::string &abs) const {
    for (const SourceFile &f : files_) {
        if (f.abs_path == abs) return f.id;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::string normalize_path(const std::string &path) {
    std::string working = path;
    if (working.empty()) working = ".";

    // Make absolute relative to the process CWD when needed.
    if (working[0] != kSeparator) {
        char buffer[4096];
        if (getcwd(buffer, sizeof(buffer))) {
            working = std::string(buffer) + kSeparator + working;
        }
    }

    // Lexical normalization: drop "." components and pop on "..".
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i <= working.size(); ++i) {
        if (i == working.size() || working[i] == kSeparator) {
            if (current == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!current.empty() && current != ".") {
                parts.push_back(current);
            }
            current.clear();
        } else {
            current += working[i];
        }
    }

    std::string result;
    for (const std::string &part : parts) {
        result += kSeparator;
        result += part;
    }
    return result.empty() ? std::string(1, kSeparator) : result;
}

std::string parent_directory(const std::string &path) {
    const std::size_t position = path.find_last_of(kSeparator);
    if (position == std::string::npos) return ".";
    if (position == 0) return std::string(1, kSeparator);
    return path.substr(0, position);
}

std::string join_path(const std::string &left, const std::string &right) {
    if (left.empty()) return right;
    if (right.empty()) return left;
    if (right[0] == kSeparator) return right;
    if (left.back() == kSeparator) return left + right;
    return left + kSeparator + right;
}

std::string path_stem(const std::string &path) {
    std::size_t begin = path.find_last_of(kSeparator);
    begin = (begin == std::string::npos) ? 0 : begin + 1;
    const std::size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && dot > begin) return path.substr(begin, dot - begin);
    return path.substr(begin);
}

bool file_exists(const std::string &path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return false;
    return S_ISREG(info.st_mode) != 0;
}

i64 file_size(const std::string &path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return -1;
    if (!S_ISREG(info.st_mode)) return -1;
    return static_cast<i64>(info.st_size);
}

bool read_file(const std::string &path, std::string &out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    out = buffer.str();
    return true;
}

bool write_file(const std::string &path, const std::string &text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

}  // namespace ppc
