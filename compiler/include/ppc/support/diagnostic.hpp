#ifndef PPC_SUPPORT_DIAGNOSTIC_HPP
#define PPC_SUPPORT_DIAGNOSTIC_HPP

#include <string>
#include <vector>

#include "ppc/support/common.hpp"
#include "ppc/support/source.hpp"

namespace ppc {

enum class Severity { Note, Warning, Error, Fatal };

const char *severity_name(Severity severity);

/// Stable diagnostic identifiers. The numeric ranges are deliberate so a code
/// tells you which phase produced it without consulting a table:
///
///   E00xx  lexer          E01xx  parser        E02xx  module / imports
///   E03xx  name resolution               E04xx  types and inference
///   E05xx  calls, arguments, overloads   E06xx  patterns and matching
///   E07xx  generics and constraints      E08xx  ownership and borrows
///   E09xx  unsupported / reserved syntax E10xx  codegen and linking
///
/// Every code here is documented in docs/diagnostics.md; `ppc explain <code>`
/// reads that file at runtime.
enum class Code : u16 {
    // Lexer
    UnexpectedCharacter = 1,
    UnterminatedString = 2,
    UnknownEscape = 3,
    UnmatchedDelimiter = 4,
    InvalidNumber = 5,

    // Parser
    UnexpectedToken = 100,
    ExpectedStatementEnd = 101,
    ReservedWord = 102,
    MixedDialect = 103,
    ExpectedType = 104,
    DuplicateParameter = 105,
    DefaultBeforeRequired = 106,
    ExpectedBlockEnd = 107,

    // Modules
    ModuleNotFound = 200,
    CircularImport = 201,
    DuplicateDefinition = 202,
    NoEntryPoint = 203,
    MultipleEntryPoints = 204,

    // Name resolution
    UnknownName = 300,
    UnknownField = 301,
    UnknownMethod = 302,
    UnknownType = 303,
    UnknownVariant = 304,
    NotCallable = 305,
    PrivateAccess = 306,

    // Types
    TypeMismatch = 400,
    CannotInfer = 401,
    NotMutable = 402,
    MissingReturn = 403,
    InvalidOperand = 404,
    ConditionNotBool = 405,
    ReturnOutsideFunction = 406,
    IndexNotInteger = 407,
    NotIndexable = 408,
    DerefNotPointer = 409,
    UnsafeRequired = 410,
    AwaitOutsideAsync = 411,
    NotAwaitable = 412,

    // Calls
    ArityMismatch = 500,
    UnknownArgument = 501,
    DuplicateArgument = 502,
    PositionalAfterNamed = 503,
    AmbiguousOverload = 504,
    NoViableOverload = 505,

    // Patterns
    NonExhaustiveMatch = 600,
    UnreachableArm = 601,
    PatternTypeMismatch = 602,
    PatternArity = 603,
    PropagateTypeMismatch = 604,

    // Generics
    ConstraintUnsatisfied = 700,
    GenericArityMismatch = 701,
    RecursiveSpecialization = 702,
    UnconstrainedParameter = 703,

    // Ownership
    UseAfterMove = 800,
    MaybeMoved = 801,
    BorrowConflict = 802,
    BorrowEscapes = 803,
    PartialMove = 804,
    MoveOfBorrowed = 805,

    // Reserved / unsupported
    FeatureReserved = 900,
    FeatureUnsupported = 901,

    /// Warnings start at 2000 and render with a W prefix, so a deprecation
    /// reads as W2000 rather than as an error code that happens to warn.
    DeprecatedSyntax = 2000,

    // Backend
    BackendUnavailable = 1000,
    LinkFailed = 1001,
    ToolchainMissing = 1002,
    BackendInternal = 1003,
};

std::string code_string(Code code);

/// A span plus a short phrase rendered underneath the caret.
struct Label {
    Span span;
    std::string message;  // may be empty for a bare underline
    bool primary = true;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    Code code = Code::UnexpectedToken;
    std::string message;
    std::vector<Label> labels;
    std::vector<std::string> notes;
    std::string help;

    Diagnostic &label(Span span, std::string text = {});
    Diagnostic &secondary(Span span, std::string text = {});
    Diagnostic &note(std::string text);
    Diagnostic &with_help(std::string text);
};

/// Collects diagnostics, renders them, and enforces the error limit.
///
/// Phases report into this and keep going where they can, so one run surfaces
/// many independent errors instead of stopping at the first. Phases that cannot
/// meaningfully continue check `has_errors()` at their own boundaries.
class DiagnosticEngine {
  public:
    explicit DiagnosticEngine(const SourceManager &sources) : sources_(sources) {}

    Diagnostic &report(Severity severity, Code code, std::string message);
    Diagnostic &error(Code code, std::string message);
    Diagnostic &warning(Code code, std::string message);

    bool has_errors() const { return error_count_ > 0; }
    std::size_t error_count() const { return error_count_; }
    std::size_t warning_count() const { return warning_count_; }
    const std::vector<Diagnostic> &all() const { return diagnostics_; }

    void set_color(bool enabled) { color_ = enabled; }
    void set_error_limit(std::size_t limit) { error_limit_ = limit; }
    /// True once the limit is hit; callers should unwind rather than pile on.
    bool limit_reached() const { return error_limit_ && error_count_ >= error_limit_; }

    /// Speculative parsing needs to try a production, and discard whatever it
    /// reported if the guess turns out wrong. `mark` records the current
    /// position; `rewind` drops everything reported since.
    std::size_t mark() const { return diagnostics_.size(); }
    void rewind(std::size_t position);

    /// Human-readable rendering with source excerpts and carets.
    std::string render() const;
    std::string render_one(const Diagnostic &diagnostic) const;
    /// Machine-readable rendering, one JSON object per diagnostic.
    std::string render_json() const;

    void clear();

  private:
    const SourceManager &sources_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t error_count_ = 0;
    std::size_t warning_count_ = 0;
    std::size_t error_limit_ = 100;
    bool color_ = false;
};

}  // namespace ppc

#endif
