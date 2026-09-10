#ifndef PPC_DRIVER_MODULE_LOADER_HPP
#define PPC_DRIVER_MODULE_LOADER_HPP

#include <string>
#include <unordered_set>
#include <vector>

#include "ppc/support/arena.hpp"
#include "ppc/support/diagnostic.hpp"
#include "ppc/syntax/ast.hpp"

namespace ppc {

/// Loads a root file and everything it imports, transitively.
///
/// Modules land in `Program::modules` in dependency order: a module always
/// appears after everything it imports. The checker relies on that only for
/// nicer diagnostics — declaration collection is order-independent — but it
/// makes the ordering predictable for `emit-ast`.
class ModuleLoader {
  public:
    ModuleLoader(SourceManager &sources, Arena &arena, Interner &interner,
                 DiagnosticEngine &diagnostics)
        : sources_(sources), arena_(arena), interner_(interner), diagnostics_(diagnostics) {}

    /// Directories searched for `std::x` and bare module names, in order.
    void add_search_path(const std::string &directory);

    /// Loads `path` and its imports. Returns nullptr when the root file cannot
    /// be opened; missing imports are reported but do not abort the load.
    Program *load(const std::string &path);

    /// Files loaded so far, in load order. Useful for build-cache fingerprints.
    const std::vector<std::string> &loaded_files() const { return loaded_; }

  private:
    /// Turns `std::math` or `std.math` into candidate filesystem paths.
    std::vector<std::string> candidate_paths(const std::vector<Symbol> &segments,
                                             const std::string &importer_directory) const;
    Module *load_file(const std::string &path, Span imported_from);

    SourceManager &sources_;
    Arena &arena_;
    Interner &interner_;
    DiagnosticEngine &diagnostics_;

    std::vector<std::string> search_paths_;
    /// Absolute paths already loaded, so a diamond import loads once.
    std::unordered_set<std::string> visited_;
    /// Absolute paths on the current DFS stack, for cycle detection.
    std::unordered_set<std::string> in_progress_;
    std::vector<std::string> loaded_;
    Program *program_ = nullptr;
};

}  // namespace ppc

#endif
