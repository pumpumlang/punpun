#ifndef PPC_SERVICE_LSP_SERVER_HPP
#define PPC_SERVICE_LSP_SERVER_HPP

#include <string>

#include "ppc/service/json.hpp"
#include "ppc/service/language_service.hpp"

namespace ppc {
namespace service {

/// Language Server Protocol over stdio.
///
/// A thin translation layer: it converts LSP requests into LanguageService
/// calls and the results back into JSON, and owns no language knowledge of its
/// own. That separation is deliberate — the PunPun 1.5 IDE is expected to use
/// LanguageService directly and skip this file entirely, while existing editors
/// get the same answers through the standard protocol.
///
/// Only implemented capabilities are advertised in `initialize`. Advertising
/// one that is not wired up makes an editor show an empty result where it
/// should show nothing, which users read as a broken server.
class LspServer {
  public:
    LspServer() = default;

    /// Serves until the client disconnects or sends `exit`.
    /// Returns the protocol-defined exit status: 0 after a `shutdown` request,
    /// 1 if the client exits without one.
    int run();

    /// Adds a module search path, e.g. the standard library, so imports resolve
    /// the same way they would in a build.
    void add_search_path(const std::string &directory) {
        service_.add_search_path(directory);
    }

  private:
    bool read_message(std::string &body);
    void write_message(const json::Value &message);
    void respond(const json::Value &id, json::Value result);
    void respond_error(const json::Value &id, int code, const std::string &message);
    void notify(const std::string &method, json::Value params);

    void publish_diagnostics(const std::string &path);

    void handle_initialize(const json::Value &id, const json::Value &params);
    void handle_hover(const json::Value &id, const json::Value &params);
    void handle_definition(const json::Value &id, const json::Value &params);
    void handle_document_symbol(const json::Value &id, const json::Value &params);
    void handle_completion(const json::Value &id, const json::Value &params);

    LanguageService service_;
    /// Set by `shutdown`, read by `exit` to pick the exit status.
    bool shutting_down_ = false;
};

}  // namespace service
}  // namespace ppc

#endif
