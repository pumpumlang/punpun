#include "ppc/driver/module_loader.hpp"

#include <algorithm>

#include "ppc/syntax/lexer.hpp"
#include "ppc/syntax/parser.hpp"

namespace ppc {

void ModuleLoader::add_search_path(const std::string &directory) {
    if (directory.empty()) return;
    const std::string normalized = normalize_path(directory);
    if (std::find(search_paths_.begin(), search_paths_.end(), normalized) != search_paths_.end()) {
        return;
    }
    search_paths_.push_back(normalized);
}

std::vector<std::string> ModuleLoader::candidate_paths(
    const std::vector<Symbol> &segments, const std::string &importer_directory) const {
    std::vector<std::string> candidates;
    if (segments.empty()) return candidates;

    // `std::math` and `std.math` are the same path; the parser already reduced
    // both spellings to a segment list.
    std::string relative;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i) relative += "/";
        relative += interner_.text(segments[i]);
    }

    // A sibling file wins over the standard library, so a project can shadow a
    // std module with its own.
    candidates.push_back(join_path(importer_directory, relative + ".pp"));
    candidates.push_back(join_path(importer_directory, join_path(relative, "main.pp")));

    for (const std::string &directory : search_paths_) {
        candidates.push_back(join_path(directory, relative + ".pp"));
        candidates.push_back(join_path(directory, join_path(relative, "main.pp")));
        // `bring math` should also find `std/math.pp`, which is how the
        // examples import stdlib modules without the `std` prefix.
        if (segments.size() == 1) {
            candidates.push_back(join_path(directory, join_path("std", relative + ".pp")));
        }
    }
    return candidates;
}

Module *ModuleLoader::load_file(const std::string &path, Span imported_from) {
    const std::string absolute = normalize_path(path);

    if (in_progress_.count(absolute)) {
        diagnostics_
            .error(Code::CircularImport, "circular import of '" + path + "'")
            .label(imported_from, "this import closes a cycle")
            .with_help("break the cycle by moving the shared declarations into a third module");
        return nullptr;
    }
    if (visited_.count(absolute)) return nullptr;  // already loaded

    auto file = sources_.load(path);
    if (!file) return nullptr;

    visited_.insert(absolute);
    in_progress_.insert(absolute);
    loaded_.push_back(absolute);

    Lexer lexer(sources_, diagnostics_, interner_);
    std::vector<Token> tokens = lexer.tokenize(*file);

    Parser parser(std::move(tokens), arena_, interner_, diagnostics_);
    Module *module = parser.parse(*file, path);

    // Imports are resolved depth-first, so a dependency is fully loaded and
    // appended before the module that needs it.
    const std::string directory = parent_directory(absolute);
    for (const ImportDecl &import : module->imports) {
        const std::vector<std::string> candidates = candidate_paths(import.segments, directory);

        bool resolved = false;
        for (const std::string &candidate : candidates) {
            if (!file_exists(candidate)) continue;
            load_file(candidate, import.span);
            resolved = true;
            break;
        }

        if (!resolved) {
            std::string spelled;
            for (std::size_t i = 0; i < import.segments.size(); ++i) {
                if (i) spelled += "::";
                spelled += interner_.text(import.segments[i]);
            }
            Diagnostic &diagnostic =
                diagnostics_.error(Code::ModuleNotFound, "cannot find module '" + spelled + "'");
            diagnostic.label(import.span);
            if (!candidates.empty()) {
                diagnostic.note("looked for " + candidates.front());
                if (candidates.size() > 1) {
                    diagnostic.note("and " + std::to_string(candidates.size() - 1) +
                                    " other location(s)");
                }
            }
            diagnostic.with_help("check the path, or pass --stdlib to point at the std modules");
        }
    }

    in_progress_.erase(absolute);
    program_->modules.push_back(module);
    return module;
}

Program *ModuleLoader::load(const std::string &path) {
    program_ = arena_.make<Program>();

    if (!file_exists(path)) {
        diagnostics_
            .error(Code::ModuleNotFound, "cannot open '" + path + "'")
            .with_help("check the path; input files use the .pp extension");
        return nullptr;
    }

    load_file(path, Span{});
    return program_;
}

}  // namespace ppc
