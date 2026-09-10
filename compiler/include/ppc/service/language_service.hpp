#ifndef PPC_SERVICE_LANGUAGE_SERVICE_HPP
#define PPC_SERVICE_LANGUAGE_SERVICE_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ppc/hir/hir.hpp"
#include "ppc/sema/checker.hpp"
#include "ppc/sema/type.hpp"
#include "ppc/support/arena.hpp"
#include "ppc/support/diagnostic.hpp"
#include "ppc/support/source.hpp"
#include "ppc/syntax/ast.hpp"

namespace ppc {
namespace service {

/// A position in a document, in the coordinates editors use.
struct Position {
    u32 line = 0;    // 0-based, as LSP defines it
    u32 column = 0;  // 0-based, in UTF-8 bytes
};

struct Range {
    Position start;
    Position end;
};

/// What a piece of source means. Kept deliberately close to LSP's SymbolKind so
/// the server is a translation rather than a second classification scheme.
enum class SymbolKind {
    Unknown,
    Function,
    Method,
    Struct,
    Object,
    Enum,
    EnumVariant,
    Field,
    Parameter,
    Local,
    Contract,
    Module,
    Builtin,
};

const char *symbol_kind_name(SymbolKind kind);

/// One named thing, and where it is defined.
struct Symbol {
    std::string name;
    /// Fully qualified where it matters, e.g. `Player::hit`.
    std::string qualified;
    SymbolKind kind = SymbolKind::Unknown;
    /// Rendered signature or type, for hover and completion detail.
    std::string detail;
    /// Where the definition lives. May be in a different file than the query.
    std::string file;
    Range range;      // the whole declaration
    Range selection;  // just the name, which is what an editor highlights
    /// Nested members: a struct's fields and methods, an enum's variants.
    std::vector<Symbol> children;
};

/// A resolved reference: some span of source, what it refers to, and its type.
struct Reference {
    Span span;
    std::string name;
    std::string type;
    SymbolKind kind = SymbolKind::Unknown;
    /// Where the referenced thing is defined; invalid when unknown.
    Span definition;
};

/// A completion candidate.
struct Completion {
    std::string label;
    std::string detail;
    SymbolKind kind = SymbolKind::Unknown;
    /// Text to insert, when it differs from the label.
    std::string insert;
};

/// One analysis of one project rooted at a file.
///
/// Holds the arena, interner, and type context that the AST and HIR point into,
/// so nothing here may outlive it. That is why the service hands out plain
/// value types (Symbol, Reference, Completion) rather than compiler nodes: an
/// editor request must not be able to retain a dangling AST pointer.
struct Analysis {
    std::unique_ptr<SourceManager> sources;
    std::unique_ptr<Interner> interner;
    std::unique_ptr<Arena> arena;
    std::unique_ptr<TypeContext> types;
    std::unique_ptr<DiagnosticEngine> diagnostics;

    Program *program = nullptr;
    HirProgram *hir = nullptr;

    /// Every resolved reference in the analysed files, sorted by span so a
    /// position lookup is a binary search rather than a tree walk.
    std::vector<Reference> references;
    /// Top-level symbols per file, for documentSymbol and workspace search.
    std::unordered_map<std::string, std::vector<Symbol>> file_symbols;

    /// True when the front end got far enough for types to be meaningful.
    /// Diagnostics are still available when false; hover and completion are
    /// degraded but not absent, because a half-typed file is the normal state
    /// in an editor.
    bool typed = false;
};

/// The compiler, exposed as queries instead of a command line.
///
/// This is the layer the 1.5 IDE is meant to build on. It deliberately owns no
/// protocol knowledge: `lsp_server.cpp` translates LSP to these calls and back.
/// A different front end — the dedicated PunPun editor, a REPL, a batch linter —
/// can use the same API without going through JSON-RPC.
///
/// It reuses the real lexer, parser, and checker. There is no separate
/// "editor parser", because a second implementation of PunPun's semantics would
/// drift from the compiler and give the IDE different answers than the build.
class LanguageService {
  public:
    LanguageService();
    ~LanguageService();

    /// Replaces the in-memory contents of a file. Overlays take priority over
    /// what is on disk, which is what lets an editor analyse unsaved buffers.
    void set_overlay(const std::string &path, std::string text);
    void clear_overlay(const std::string &path);

    /// Adds a directory to the module search path, e.g. the standard library.
    void add_search_path(const std::string &directory);

    /// Runs the front end on `path` and caches the result. Re-runs only when
    /// the file or an overlay changed, so repeated queries on an idle buffer
    /// are free.
    const Analysis &analyze(const std::string &path);
    /// Discards the cached analysis for a file.
    void invalidate(const std::string &path);

    // -- queries ------------------------------------------------------------

    /// Diagnostics for `path`, from the real compiler.
    const std::vector<Diagnostic> &diagnostics(const std::string &path);

    /// The innermost resolved reference containing `position`, if any.
    const Reference *reference_at(const std::string &path, Position position);

    /// Hover text: what the thing under the cursor is and its type.
    /// Empty when there is nothing meaningful to say.
    std::string hover(const std::string &path, Position position);

    /// Where the thing under the cursor is defined.
    bool definition(const std::string &path, Position position, std::string &file,
                    Range &range);

    /// Every reference to whatever is under the cursor, within the analysed
    /// files. Not a whole-workspace search: it covers what was compiled.
    std::vector<std::pair<std::string, Range>> references_to(const std::string &path,
                                                             Position position);

    /// Symbols declared in `path`, nested.
    std::vector<Symbol> document_symbols(const std::string &path);

    /// Completions at `position`. Semantic: locals actually in scope, then
    /// program declarations, then builtins. Never text matching.
    std::vector<Completion> complete(const std::string &path, Position position);

    /// Converts between editor coordinates and compiler spans.
    static Position to_position(const SourceManager &sources, FileId file, u32 offset);
    static Range to_range(const SourceManager &sources, Span span);
    static u32 to_offset(const SourceManager &sources, FileId file, Position position);

  private:
    struct Cached;
    std::unordered_map<std::string, std::unique_ptr<Cached>> cache_;
    std::unordered_map<std::string, std::string> overlays_;
    std::vector<std::string> search_paths_;
    /// Bumped on every overlay change, so a cached analysis knows it is stale
    /// without comparing file contents.
    u64 revision_ = 0;

    void build_index(Analysis &analysis);
    void index_module(Analysis &analysis, const Module &module);
    void index_hir(Analysis &analysis);
};

}  // namespace service
}  // namespace ppc

#endif
