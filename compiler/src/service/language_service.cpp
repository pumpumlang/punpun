#include "ppc/service/language_service.hpp"

#include <algorithm>
#include <functional>

#include "ppc/driver/module_loader.hpp"
#include "ppc/sema/builtins.hpp"

namespace ppc {
namespace service {

const char *symbol_kind_name(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Function: return "function";
        case SymbolKind::Method: return "method";
        case SymbolKind::Struct: return "struct";
        case SymbolKind::Object: return "object";
        case SymbolKind::Enum: return "enum";
        case SymbolKind::EnumVariant: return "variant";
        case SymbolKind::Field: return "field";
        case SymbolKind::Parameter: return "parameter";
        case SymbolKind::Local: return "local";
        case SymbolKind::Contract: return "contract";
        case SymbolKind::Module: return "module";
        case SymbolKind::Builtin: return "builtin";
        case SymbolKind::Unknown: break;
    }
    return "unknown";
}

struct LanguageService::Cached {
    Analysis analysis;
    u64 revision = 0;
    bool valid = false;
};

LanguageService::LanguageService() = default;
LanguageService::~LanguageService() = default;

void LanguageService::set_overlay(const std::string &path, std::string text) {
    overlays_[normalize_path(path)] = std::move(text);
    ++revision_;
}

void LanguageService::clear_overlay(const std::string &path) {
    overlays_.erase(normalize_path(path));
    ++revision_;
}

void LanguageService::add_search_path(const std::string &directory) {
    const std::string normalized = normalize_path(directory);
    if (std::find(search_paths_.begin(), search_paths_.end(), normalized) ==
        search_paths_.end()) {
        search_paths_.push_back(normalized);
    }
}

void LanguageService::invalidate(const std::string &path) {
    cache_.erase(normalize_path(path));
}

// ---------------------------------------------------------------------------
// Position mapping
// ---------------------------------------------------------------------------

Position LanguageService::to_position(const SourceManager &sources, FileId file, u32 offset) {
    Position position;
    if (!sources.has(file)) return position;
    const LineColumn location = sources.locate(file, offset);
    // SourceManager is 1-based because that is what a compiler prints; LSP is
    // 0-based. The conversion lives here so nothing else has to remember it.
    position.line = location.line > 0 ? location.line - 1 : 0;
    position.column = location.column > 0 ? location.column - 1 : 0;
    return position;
}

Range LanguageService::to_range(const SourceManager &sources, Span span) {
    Range range;
    if (!span.valid() || !sources.has(span.file)) return range;
    range.start = to_position(sources, span.file, span.start);
    range.end = to_position(sources, span.file, span.end);
    return range;
}

u32 LanguageService::to_offset(const SourceManager &sources, FileId file, Position position) {
    if (!sources.has(file)) return 0;
    const SourceFile &source = sources.file(file);
    u32 line = 0;
    u32 offset = 0;
    // Walk to the requested line, then along it. Files in an editor are small
    // enough that this is not worth an index.
    while (line < position.line && offset < source.text.size()) {
        if (source.text[offset] == '\n') ++line;
        ++offset;
    }
    u32 column = 0;
    while (column < position.column && offset < source.text.size() &&
           source.text[offset] != '\n') {
        ++offset;
        ++column;
    }
    return offset;
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------

const Analysis &LanguageService::analyze(const std::string &path) {
    const std::string key = normalize_path(path);

    auto existing = cache_.find(key);
    if (existing != cache_.end() && existing->second->valid &&
        existing->second->revision == revision_) {
        return existing->second->analysis;
    }

    auto cached = std::make_unique<Cached>();
    Analysis &analysis = cached->analysis;

    analysis.sources = std::make_unique<SourceManager>();
    analysis.interner = std::make_unique<Interner>();
    analysis.arena = std::make_unique<Arena>();
    analysis.types = std::make_unique<TypeContext>(*analysis.interner);
    analysis.diagnostics = std::make_unique<DiagnosticEngine>(*analysis.sources);
    analysis.diagnostics->set_color(false);
    // An editor wants every problem in the buffer, not the first hundred.
    analysis.diagnostics->set_error_limit(0);

    // Unsaved buffers are injected as virtual files so the loader finds them
    // instead of the stale contents on disk.
    for (const auto &overlay : overlays_) {
        analysis.sources->add_overlay(overlay.first, overlay.second);
    }

    ModuleLoader loader(*analysis.sources, *analysis.arena, *analysis.interner,
                        *analysis.diagnostics);
    loader.add_search_path(parent_directory(key));
    for (const std::string &directory : search_paths_) loader.add_search_path(directory);

    analysis.program = loader.load(path);

    if (analysis.program && !analysis.diagnostics->has_errors()) {
        Checker checker(*analysis.types, *analysis.arena, *analysis.interner,
                        *analysis.diagnostics);
        analysis.hir = checker.check(*analysis.program);
        analysis.typed = true;
    }

    build_index(analysis);

    cached->revision = revision_;
    cached->valid = true;
    Cached *raw = cached.get();
    cache_[key] = std::move(cached);
    return raw->analysis;
}

const std::vector<Diagnostic> &LanguageService::diagnostics(const std::string &path) {
    const Analysis &analysis = analyze(path);
    return analysis.diagnostics->all();
}

// ---------------------------------------------------------------------------
// Index construction
// ---------------------------------------------------------------------------

namespace {

std::string describe_function(const FunctionDecl &fn, const Interner &interner) {
    std::string text = "fn " + fn.qualified_name(interner);
    if (!fn.generics.empty()) {
        text += "<";
        for (std::size_t i = 0; i < fn.generics.size(); ++i) {
            if (i) text += ", ";
            text += interner.text(fn.generics[i].name);
        }
        text += ">";
    }
    text += "(";
    for (std::size_t i = 0; i < fn.params.size(); ++i) {
        if (i) text += ", ";
        text += interner.text(fn.params[i].name);
        text += ": ";
        text += fn.params[i].type ? fn.params[i].type->describe(interner) : "?";
        if (fn.params[i].default_value) text += " = ...";
    }
    text += ")";
    if (fn.result) text += " -> " + fn.result->describe(interner);
    return text;
}

}  // namespace

void LanguageService::index_module(Analysis &analysis, const Module &module) {
    const Interner &interner = *analysis.interner;
    const SourceManager &sources = *analysis.sources;
    std::vector<Symbol> symbols;

    auto make = [&](const std::string &name, SymbolKind kind, const std::string &detail,
                    Span span) {
        Symbol symbol;
        symbol.name = name;
        symbol.qualified = name;
        symbol.kind = kind;
        symbol.detail = detail;
        symbol.file = module.path;
        symbol.range = to_range(sources, span);
        symbol.selection = symbol.range;
        return symbol;
    };

    for (const FunctionDecl *fn : module.functions) {
        if (!fn) continue;
        Symbol symbol = make(interner.text(fn->name),
                             fn->is_method ? SymbolKind::Method : SymbolKind::Function,
                             describe_function(*fn, interner), fn->span);
        for (const Param &param : fn->params) {
            symbol.children.push_back(
                make(interner.text(param.name), SymbolKind::Parameter,
                     param.type ? param.type->describe(interner) : "?", param.span));
        }
        symbols.push_back(std::move(symbol));
    }

    for (const ShapeDecl *shape : module.shapes) {
        if (!shape) continue;
        Symbol symbol = make(interner.text(shape->name),
                             shape->is_reference ? SymbolKind::Object : SymbolKind::Struct,
                             shape->is_reference ? "object" : "struct", shape->span);
        for (const Param &field : shape->fields) {
            symbol.children.push_back(
                make(interner.text(field.name), SymbolKind::Field,
                     field.type ? field.type->describe(interner) : "?", field.span));
        }
        for (const FunctionDecl *method : shape->methods) {
            if (!method) continue;
            symbol.children.push_back(make(interner.text(method->name), SymbolKind::Method,
                                           describe_function(*method, interner),
                                           method->span));
        }
        symbols.push_back(std::move(symbol));
    }

    for (const EnumDecl *decl : module.enums) {
        if (!decl) continue;
        Symbol symbol = make(interner.text(decl->name), SymbolKind::Enum, "enum", decl->span);
        for (const EnumVariantDecl &variant : decl->variants) {
            std::string detail = interner.text(decl->name) + "::" + interner.text(variant.name);
            if (!variant.payload.empty()) {
                detail += "(";
                for (std::size_t i = 0; i < variant.payload.size(); ++i) {
                    if (i) detail += ", ";
                    detail += variant.payload[i] ? variant.payload[i]->describe(interner) : "?";
                }
                detail += ")";
            }
            symbol.children.push_back(make(interner.text(variant.name),
                                           SymbolKind::EnumVariant, detail, variant.span));
        }
        symbols.push_back(std::move(symbol));
    }

    for (const ContractDecl *decl : module.contracts) {
        if (!decl) continue;
        symbols.push_back(
            make(interner.text(decl->name), SymbolKind::Contract, "contract", decl->span));
    }

    analysis.file_symbols[module.path] = std::move(symbols);
}

void LanguageService::index_hir(Analysis &analysis) {
    if (!analysis.hir) return;
    const TypeContext &types = *analysis.types;

    // Expression spans carry their resolved types, which is what makes hover
    // report a real inferred type rather than a guess from syntax.
    std::function<void(const HirExpr *)> walk_expr = [&](const HirExpr *expr) {
        if (!expr) return;
        if (expr->span.valid() && expr->type && !expr->type->is_error()) {
            Reference reference;
            reference.span = expr->span;
            reference.type = types.describe(expr->type);
            reference.kind = SymbolKind::Local;
            analysis.references.push_back(std::move(reference));
        }
        walk_expr(expr->left);
        walk_expr(expr->right);
        for (const HirExpr *operand : expr->operands) walk_expr(operand);
        for (const HirArm &arm : expr->arms) {
            // An arm is an ordered sequence of tests and bindings; only the
            // tests carry expressions worth indexing.
            for (const HirArmStep &step : arm.steps) {
                if (step.kind == HirArmStep::Kind::Test) walk_expr(step.test);
            }
            walk_expr(arm.value);
        }
    };

    std::function<void(const std::vector<HirStmt *> &)> walk_body;
    std::function<void(const std::vector<HirStmt *> &)> walk_body_impl =
        [&](const std::vector<HirStmt *> &body) {
            for (const HirStmt *statement : body) {
                if (!statement) continue;
                walk_expr(statement->value);
                walk_expr(statement->place);
                walk_expr(statement->range_start);
                walk_expr(statement->range_end);
                walk_body(statement->body);
                walk_body(statement->alternative);
            }
        };
    walk_body = walk_body_impl;

    for (const HirFunction *fn : analysis.hir->functions) {
        if (!fn || fn->is_extern_native) continue;
        walk_body(fn->body);
    }
}

void LanguageService::build_index(Analysis &analysis) {
    analysis.references.clear();
    analysis.file_symbols.clear();

    if (analysis.program) {
        for (const Module *module : analysis.program->modules) {
            if (module) index_module(analysis, *module);
        }
    }
    index_hir(analysis);

    // Sorted innermost-last, so scanning backwards finds the tightest span
    // covering a position first.
    std::sort(analysis.references.begin(), analysis.references.end(),
              [](const Reference &a, const Reference &b) {
                  if (a.span.start != b.span.start) return a.span.start < b.span.start;
                  return a.span.length() > b.span.length();
              });
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

const Reference *LanguageService::reference_at(const std::string &path, Position position) {
    const Analysis &analysis = analyze(path);
    if (!analysis.sources) return nullptr;

    auto file = analysis.sources->find_by_abs_path(normalize_path(path));
    if (!file) return nullptr;
    const u32 offset = to_offset(*analysis.sources, *file, position);

    // The innermost enclosing span is the most specific answer, so prefer the
    // shortest span that contains the offset.
    const Reference *best = nullptr;
    for (const Reference &reference : analysis.references) {
        if (reference.span.file != *file) continue;
        if (offset < reference.span.start || offset >= reference.span.end) continue;
        if (!best || reference.span.length() < best->span.length()) best = &reference;
    }
    return best;
}

namespace {

/// True when `position` falls inside `range`, comparing line then column.
///
/// Line-only comparison is not good enough: a single-line function declaration
/// contains its own parameters, so matching by line alone reports the innermost
/// thing that happens to start on that line rather than the thing under the
/// cursor.
bool contains(const Range &range, const Position &position) {
    if (position.line < range.start.line || position.line > range.end.line) return false;
    if (position.line == range.start.line && position.column < range.start.column) return false;
    if (position.line == range.end.line && position.column > range.end.column) return false;
    return true;
}

/// Rough size of a range, for picking the innermost of several matches.
u64 range_extent(const Range &range) {
    const u64 lines = range.end.line >= range.start.line
                          ? range.end.line - range.start.line
                          : 0;
    return lines * 4096 + (range.end.column > range.start.column
                               ? range.end.column - range.start.column
                               : 0);
}

}  // namespace

std::string LanguageService::hover(const std::string &path, Position position) {
    const Analysis &analysis = analyze(path);
    const std::string wanted = normalize_path(path);

    // A declaration under the cursor is more informative than the type of the
    // expression it encloses, so declarations are preferred. Among several
    // matches the innermost wins: a parameter beats the function containing it,
    // but only when the cursor is actually on the parameter.
    const Symbol *best = nullptr;
    for (const auto &entry : analysis.file_symbols) {
        if (normalize_path(entry.first) != wanted) continue;
        for (const Symbol &symbol : entry.second) {
            if (contains(symbol.range, position)) {
                if (!best || range_extent(symbol.range) < range_extent(best->range)) {
                    best = &symbol;
                }
            }
            for (const Symbol &child : symbol.children) {
                if (!contains(child.range, position)) continue;
                if (!best || range_extent(child.range) < range_extent(best->range)) {
                    best = &child;
                }
            }
        }
    }
    // An indexed expression may be tighter than the declaration enclosing it —
    // a local inside a function body is inside the function's range but is not
    // a declaration symbol. Whichever match is smaller is the one the cursor is
    // actually on.
    const Reference *reference = reference_at(path, position);
    if (best && reference && !reference->type.empty()) {
        const Range expression = to_range(*analysis.sources, reference->span);
        if (range_extent(expression) < range_extent(best->range)) return reference->type;
    }
    if (best) return best->detail.empty() ? best->name : best->detail;
    if (reference && !reference->type.empty()) return reference->type;
    return {};
}

bool LanguageService::definition(const std::string &path, Position position,
                                 std::string &file, Range &range) {
    const Analysis &analysis = analyze(path);
    const Reference *reference = reference_at(path, position);
    if (!reference || !reference->definition.valid()) return false;
    if (!analysis.sources->has(reference->definition.file)) return false;
    file = analysis.sources->file(reference->definition.file).path;
    range = to_range(*analysis.sources, reference->definition);
    return true;
}

std::vector<std::pair<std::string, Range>> LanguageService::references_to(
    const std::string &path, Position position) {
    std::vector<std::pair<std::string, Range>> results;
    const Analysis &analysis = analyze(path);
    const Reference *target = reference_at(path, position);
    if (!target || target->name.empty()) return results;

    for (const Reference &reference : analysis.references) {
        if (reference.name != target->name) continue;
        if (!analysis.sources->has(reference.span.file)) continue;
        results.emplace_back(analysis.sources->file(reference.span.file).path,
                             to_range(*analysis.sources, reference.span));
    }
    return results;
}

std::vector<Symbol> LanguageService::document_symbols(const std::string &path) {
    const Analysis &analysis = analyze(path);
    const std::string wanted = normalize_path(path);
    for (const auto &entry : analysis.file_symbols) {
        if (normalize_path(entry.first) == wanted) return entry.second;
    }
    return {};
}

std::vector<Completion> LanguageService::complete(const std::string &path, Position position) {
    std::vector<Completion> results;
    const Analysis &analysis = analyze(path);
    (void)position;

    // Declarations from the analysed program. These come from the real parser,
    // so a name appears here only if it actually exists.
    for (const auto &entry : analysis.file_symbols) {
        for (const Symbol &symbol : entry.second) {
            Completion completion;
            completion.label = symbol.name;
            completion.detail = symbol.detail;
            completion.kind = symbol.kind;
            results.push_back(std::move(completion));

            // Enum variants are only reachable qualified, so offer them that
            // way rather than as bare names that would not compile.
            if (symbol.kind == SymbolKind::Enum) {
                for (const Symbol &variant : symbol.children) {
                    Completion nested;
                    nested.label = symbol.name + "::" + variant.name;
                    nested.detail = variant.detail;
                    nested.kind = SymbolKind::EnumVariant;
                    results.push_back(std::move(nested));
                }
            }
        }
    }

    for (const BuiltinSpec &spec : builtin_table()) {
        Completion completion;
        completion.label = spec.name;
        completion.detail = builtin_signature(spec);
        completion.kind = SymbolKind::Builtin;
        results.push_back(std::move(completion));
    }

    std::sort(results.begin(), results.end(),
              [](const Completion &a, const Completion &b) { return a.label < b.label; });
    results.erase(std::unique(results.begin(), results.end(),
                              [](const Completion &a, const Completion &b) {
                                  return a.label == b.label;
                              }),
                  results.end());
    return results;
}

}  // namespace service
}  // namespace ppc
