#include "ppc/service/lsp_server.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>

#include "ppc/service/json.hpp"
#include "ppc/support/version.hpp"

namespace ppc {
namespace service {

namespace {

/// LSP DiagnosticSeverity.
int lsp_severity(Severity severity) {
    switch (severity) {
        case Severity::Error:
        case Severity::Fatal: return 1;
        case Severity::Warning: return 2;
        case Severity::Note: return 3;
    }
    return 1;
}

/// LSP SymbolKind. Values are from the protocol specification; PunPun concepts
/// are mapped to the nearest one a generic editor already knows how to render.
int lsp_symbol_kind(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Function: return 12;
        case SymbolKind::Method: return 6;
        case SymbolKind::Struct: return 23;
        case SymbolKind::Object: return 5;   // Class: has identity and methods
        case SymbolKind::Enum: return 10;
        case SymbolKind::EnumVariant: return 22;
        case SymbolKind::Field: return 8;
        case SymbolKind::Parameter: return 13;
        case SymbolKind::Local: return 13;
        case SymbolKind::Contract: return 11;  // Interface
        case SymbolKind::Module: return 2;
        case SymbolKind::Builtin: return 12;
        case SymbolKind::Unknown: break;
    }
    return 13;
}

/// LSP CompletionItemKind.
int lsp_completion_kind(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Function:
        case SymbolKind::Builtin: return 3;
        case SymbolKind::Method: return 2;
        case SymbolKind::Field: return 5;
        case SymbolKind::Struct:
        case SymbolKind::Object: return 22;
        case SymbolKind::Enum: return 13;
        case SymbolKind::EnumVariant: return 20;
        case SymbolKind::Contract: return 8;
        default: return 6;  // Variable
    }
}

json::Value encode_position(const Position &position) {
    json::Value value = json::Value::object();
    value.set("line", json::Value(static_cast<int>(position.line)));
    value.set("character", json::Value(static_cast<int>(position.column)));
    return value;
}

json::Value encode_range(const Range &range) {
    json::Value value = json::Value::object();
    value.set("start", encode_position(range.start));
    value.set("end", encode_position(range.end));
    return value;
}

Position decode_position(const json::Value &value) {
    Position position;
    position.line = static_cast<u32>(value["line"].as_int(0));
    position.column = static_cast<u32>(value["character"].as_int(0));
    return position;
}

/// Converts a `file://` URI to a filesystem path.
///
/// Only the file scheme is handled, which is all a compiler can open. Percent
/// escapes are decoded because editors escape spaces in paths.
std::string uri_to_path(const std::string &uri) {
    std::string path = uri;
    if (path.rfind("file://", 0) == 0) path = path.substr(7);

    std::string decoded;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) {
            const std::string hex = path.substr(i + 1, 2);
            decoded += static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            i += 2;
        } else {
            decoded += path[i];
        }
    }
    // A Windows URI looks like file:///C:/x, leaving a leading slash before the
    // drive letter that must be dropped.
    if (decoded.size() > 2 && decoded[0] == '/' && decoded[2] == ':') {
        decoded = decoded.substr(1);
    }
    return decoded;
}

std::string path_to_uri(const std::string &path) {
    std::string absolute = normalize_path(path);
    std::string uri = "file://";
    if (!absolute.empty() && absolute[0] != '/') uri += "/";
    for (char c : absolute) {
        // Space is the escape that matters in practice; the rest of the
        // unreserved set passes through.
        if (c == ' ') uri += "%20";
        else if (c == '\\') uri += '/';
        else uri += c;
    }
    return uri;
}

}  // namespace

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

bool LspServer::read_message(std::string &body) {
    // LSP frames every message with HTTP-style headers. Only Content-Length is
    // required; anything else is read and ignored.
    std::size_t length = 0;
    std::string line;

    while (true) {
        line.clear();
        int c;
        while ((c = std::fgetc(stdin)) != EOF) {
            if (c == '\n') break;
            if (c != '\r') line += static_cast<char>(c);
        }
        if (c == EOF && line.empty()) return false;

        if (line.empty()) break;  // blank line ends the headers

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        for (char &n : name) n = static_cast<char>(std::tolower(n));
        if (name == "content-length") {
            length = static_cast<std::size_t>(std::strtoul(line.c_str() + colon + 1, nullptr, 10));
        }
    }

    if (length == 0) return false;
    body.assign(length, '\0');
    std::size_t read = 0;
    while (read < length) {
        const std::size_t got = std::fread(&body[read], 1, length - read, stdin);
        if (got == 0) return false;
        read += got;
    }
    return true;
}

void LspServer::write_message(const json::Value &message) {
    const std::string body = message.dump();
    std::fprintf(stdout, "Content-Length: %zu\r\n\r\n%s", body.size(), body.c_str());
    // Unbuffered delivery matters: a client blocked waiting for a response it
    // has already been sent but not flushed looks like a hung server.
    std::fflush(stdout);
}

void LspServer::respond(const json::Value &id, json::Value result) {
    json::Value message = json::Value::object();
    message.set("jsonrpc", json::Value("2.0"));
    message.set("id", id);
    message.set("result", std::move(result));
    write_message(message);
}

void LspServer::respond_error(const json::Value &id, int code, const std::string &message) {
    json::Value error = json::Value::object();
    error.set("code", json::Value(code));
    error.set("message", json::Value(message));

    json::Value response = json::Value::object();
    response.set("jsonrpc", json::Value("2.0"));
    response.set("id", id);
    response.set("error", std::move(error));
    write_message(response);
}

void LspServer::notify(const std::string &method, json::Value params) {
    json::Value message = json::Value::object();
    message.set("jsonrpc", json::Value("2.0"));
    message.set("method", json::Value(method));
    message.set("params", std::move(params));
    write_message(message);
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void LspServer::publish_diagnostics(const std::string &path) {
    const Analysis &analysis = service_.analyze(path);

    json::Value items = json::Value::array();
    for (const Diagnostic &diagnostic : analysis.diagnostics->all()) {
        // The primary label carries the span the user should look at. A
        // diagnostic with no label has nowhere to point, so it is reported at
        // the start of the file rather than dropped.
        Span span;
        for (const Label &label : diagnostic.labels) {
            if (label.primary && label.span.valid()) {
                span = label.span;
                break;
            }
        }
        if (!span.valid() && !diagnostic.labels.empty()) span = diagnostic.labels[0].span;

        // Only diagnostics belonging to this file go in this notification.
        if (span.valid() && analysis.sources->has(span.file)) {
            const std::string owner = normalize_path(analysis.sources->file(span.file).path);
            if (owner != normalize_path(path)) continue;
        }

        json::Value item = json::Value::object();
        item.set("range", encode_range(LanguageService::to_range(*analysis.sources, span)));
        item.set("severity", json::Value(lsp_severity(diagnostic.severity)));
        item.set("code", json::Value(code_string(diagnostic.code)));
        item.set("source", json::Value("ppc"));

        // Notes and help are folded into the message: an editor shows one
        // string, and dropping the help would lose the most actionable part.
        std::string message = diagnostic.message;
        for (const std::string &note : diagnostic.notes) message += "\nnote: " + note;
        if (!diagnostic.help.empty()) message += "\nhelp: " + diagnostic.help;
        item.set("message", json::Value(message));

        // Secondary labels become related information, so an editor can jump to
        // the earlier declaration a borrow or move error refers to.
        json::Value related = json::Value::array();
        for (const Label &label : diagnostic.labels) {
            if (label.primary || !label.span.valid()) continue;
            if (!analysis.sources->has(label.span.file)) continue;
            json::Value location = json::Value::object();
            location.set("uri",
                         json::Value(path_to_uri(analysis.sources->file(label.span.file).path)));
            location.set("range",
                         encode_range(LanguageService::to_range(*analysis.sources, label.span)));

            json::Value entry = json::Value::object();
            entry.set("location", std::move(location));
            entry.set("message", json::Value(label.message.empty() ? "related" : label.message));
            related.push(std::move(entry));
        }
        if (!related.elements().empty()) item.set("relatedInformation", std::move(related));

        items.push(std::move(item));
    }

    json::Value params = json::Value::object();
    params.set("uri", json::Value(path_to_uri(path)));
    params.set("diagnostics", std::move(items));
    notify("textDocument/publishDiagnostics", std::move(params));
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

void LspServer::handle_initialize(const json::Value &id, const json::Value &params) {
    // Only capabilities that are actually implemented are advertised. Claiming
    // one that is not wired up makes an editor show an empty result where it
    // should show nothing at all, which reads as a broken server.
    json::Value sync = json::Value::object();
    sync.set("openClose", json::Value(true));
    // Full sync: the compiler re-analyses whole files anyway, so incremental
    // sync would add bookkeeping for no benefit.
    sync.set("change", json::Value(1));
    sync.set("save", json::Value(true));

    json::Value completion = json::Value::object();
    completion.set("resolveProvider", json::Value(false));

    json::Value capabilities = json::Value::object();
    capabilities.set("textDocumentSync", std::move(sync));
    capabilities.set("hoverProvider", json::Value(true));
    capabilities.set("definitionProvider", json::Value(true));
    capabilities.set("documentSymbolProvider", json::Value(true));
    capabilities.set("completionProvider", std::move(completion));

    json::Value info = json::Value::object();
    info.set("name", json::Value("ppc"));
    info.set("version", json::Value(version::kCompilerVersion));

    json::Value result = json::Value::object();
    result.set("capabilities", std::move(capabilities));
    result.set("serverInfo", std::move(info));
    respond(id, std::move(result));

    // The workspace root lets imports resolve the way a build would.
    const std::string root = params["rootPath"].as_string();
    if (!root.empty()) service_.add_search_path(root);
}

void LspServer::handle_hover(const json::Value &id, const json::Value &params) {
    const std::string path = uri_to_path(params["textDocument"]["uri"].as_string());
    const Position position = decode_position(params["position"]);

    const std::string text = service_.hover(path, position);
    if (text.empty()) {
        respond(id, json::Value());
        return;
    }

    json::Value contents = json::Value::object();
    contents.set("kind", json::Value("markdown"));
    contents.set("value", json::Value("```punpun\n" + text + "\n```"));

    json::Value result = json::Value::object();
    result.set("contents", std::move(contents));
    respond(id, std::move(result));
}

void LspServer::handle_definition(const json::Value &id, const json::Value &params) {
    const std::string path = uri_to_path(params["textDocument"]["uri"].as_string());
    const Position position = decode_position(params["position"]);

    std::string file;
    Range range;
    if (!service_.definition(path, position, file, range)) {
        respond(id, json::Value());
        return;
    }

    json::Value location = json::Value::object();
    location.set("uri", json::Value(path_to_uri(file)));
    location.set("range", encode_range(range));
    respond(id, std::move(location));
}

void LspServer::handle_document_symbol(const json::Value &id, const json::Value &params) {
    const std::string path = uri_to_path(params["textDocument"]["uri"].as_string());

    json::Value items = json::Value::array();
    for (const Symbol &symbol : service_.document_symbols(path)) {
        json::Value item = json::Value::object();
        item.set("name", json::Value(symbol.name));
        item.set("detail", json::Value(symbol.detail));
        item.set("kind", json::Value(lsp_symbol_kind(symbol.kind)));
        item.set("range", encode_range(symbol.range));
        item.set("selectionRange", encode_range(symbol.selection));

        json::Value children = json::Value::array();
        for (const Symbol &child : symbol.children) {
            json::Value entry = json::Value::object();
            entry.set("name", json::Value(child.name));
            entry.set("detail", json::Value(child.detail));
            entry.set("kind", json::Value(lsp_symbol_kind(child.kind)));
            entry.set("range", encode_range(child.range));
            entry.set("selectionRange", encode_range(child.selection));
            children.push(std::move(entry));
        }
        if (!children.elements().empty()) item.set("children", std::move(children));

        items.push(std::move(item));
    }
    respond(id, std::move(items));
}

void LspServer::handle_completion(const json::Value &id, const json::Value &params) {
    const std::string path = uri_to_path(params["textDocument"]["uri"].as_string());
    const Position position = decode_position(params["position"]);

    json::Value items = json::Value::array();
    for (const Completion &completion : service_.complete(path, position)) {
        json::Value item = json::Value::object();
        item.set("label", json::Value(completion.label));
        item.set("detail", json::Value(completion.detail));
        item.set("kind", json::Value(lsp_completion_kind(completion.kind)));
        if (!completion.insert.empty()) {
            item.set("insertText", json::Value(completion.insert));
        }
        items.push(std::move(item));
    }
    respond(id, std::move(items));
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

int LspServer::run() {
    std::string body;
    while (read_message(body)) {
        std::string error;
        const json::Value message = json::parse(body, error);
        if (!error.empty()) {
            // A malformed message is the client's problem, not a reason to die.
            continue;
        }

        const std::string method = message["method"].as_string();
        const json::Value &id = message["id"];
        const json::Value &params = message["params"];
        const bool is_request = !id.is_null();

        if (method == "initialize") {
            handle_initialize(id, params);
        } else if (method == "initialized") {
            // Nothing to do; acknowledged by protocol.
        } else if (method == "shutdown") {
            shutting_down_ = true;
            respond(id, json::Value());
        } else if (method == "exit") {
            // Exit status is defined by the protocol: 0 after a shutdown
            // request, 1 otherwise.
            return shutting_down_ ? 0 : 1;
        } else if (method == "textDocument/didOpen") {
            const std::string path = uri_to_path(params["textDocument"]["uri"].as_string());
            service_.set_overlay(path, params["textDocument"]["text"].as_string());
            publish_diagnostics(path);
        } else if (method == "textDocument/didChange") {
            const std::string path = uri_to_path(params["textDocument"]["uri"].as_string());
            // Full sync was advertised, so the last change carries the whole
            // document.
            const auto &changes = params["contentChanges"].elements();
            if (!changes.empty()) {
                service_.set_overlay(path, changes.back()["text"].as_string());
                publish_diagnostics(path);
            }
        } else if (method == "textDocument/didSave") {
            publish_diagnostics(uri_to_path(params["textDocument"]["uri"].as_string()));
        } else if (method == "textDocument/didClose") {
            const std::string path = uri_to_path(params["textDocument"]["uri"].as_string());
            service_.clear_overlay(path);
            // Clearing the list removes the squiggles for a file the editor is
            // no longer showing.
            json::Value clear = json::Value::object();
            clear.set("uri", json::Value(path_to_uri(path)));
            clear.set("diagnostics", json::Value::array());
            notify("textDocument/publishDiagnostics", std::move(clear));
        } else if (method == "textDocument/hover") {
            handle_hover(id, params);
        } else if (method == "textDocument/definition") {
            handle_definition(id, params);
        } else if (method == "textDocument/documentSymbol") {
            handle_document_symbol(id, params);
        } else if (method == "textDocument/completion") {
            handle_completion(id, params);
        } else if (is_request) {
            // MethodNotFound. Notifications get no reply, per JSON-RPC.
            respond_error(id, -32601, "ppc does not implement " + method);
        }
    }
    return 0;
}

}  // namespace service
}  // namespace ppc
