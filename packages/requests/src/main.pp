import std.net.https

# The 1.0 requests surface is preserved, but its implementation is now the
# compiler/runtime HTTPS service rather than package-local injected C.
object HttpResponse {
    public let status: i64;
    public let body: String;
    public let error: String;

    public init(status: i64, body: String, error: String) {
        self.status = status;
        self.body = body;
        self.error = error;
    }

    public fn ok() -> bool {
        return self.status >= 200 && self.status < 300 && len(self.error) == 0;
    }
    public fn text() -> String { return self.body; }
}

fn requests_available() -> bool { return https_available(); }

fn requests_request(method: String, url: String, body: String, headers: String,
                    timeout_ms: i64, follow_redirects: bool) -> HttpResponse {
    let response_body = https_request(method, url, body, headers, timeout_ms,
                                      follow_redirects);
    return HttpResponse(https_status(), response_body, https_error());
}

fn requests_get(url: String) -> HttpResponse {
    return requests_request("GET", url, "", "", 30000, true);
}
fn requests_post(url: String, body: String) -> HttpResponse {
    return requests_request("POST", url, body, "", 30000, true);
}
fn requests_put(url: String, body: String) -> HttpResponse {
    return requests_request("PUT", url, body, "", 30000, true);
}
fn requests_patch(url: String, body: String) -> HttpResponse {
    return requests_request("PATCH", url, body, "", 30000, true);
}
fn requests_delete(url: String) -> HttpResponse {
    return requests_request("DELETE", url, "", "", 30000, true);
}
fn requests_head(url: String) -> HttpResponse {
    return requests_request("HEAD", url, "", "", 30000, true);
}

async fn requests_request_async(method: String, url: String, body: String,
                                headers: String, timeout_ms: i64,
                                follow_redirects: bool) -> HttpResponse {
    return requests_request(method, url, body, headers, timeout_ms,
                            follow_redirects);
}
async fn requests_get_async(url: String) -> HttpResponse {
    return requests_get(url);
}
async fn requests_post_async(url: String, body: String) -> HttpResponse {
    return requests_post(url, body);
}
async fn requests_put_async(url: String, body: String) -> HttpResponse {
    return requests_put(url, body);
}
async fn requests_patch_async(url: String, body: String) -> HttpResponse {
    return requests_patch(url, body);
}
async fn requests_delete_async(url: String) -> HttpResponse {
    return requests_delete(url);
}
async fn requests_head_async(url: String) -> HttpResponse {
    return requests_head(url);
}
