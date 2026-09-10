# Verified HTTPS helpers.
#
# The primitive operations are compiler builtins backed by the system libcurl
# runtime. Certificate and hostname verification are always enabled, redirects
# remain HTTPS-only, and plain HTTP URLs are rejected.

fn https_get(url: String) -> String {
    return https_request("GET", url, "", "", 30000, true);
}

fn https_get_with_headers(url: String, headers: String) -> String {
    return https_request("GET", url, "", headers, 30000, true);
}

fn https_post(url: String, body: String, content_type: String) -> String {
    let headers = concat("Content-Type: ", content_type);
    return https_request("POST", url, body, headers, 30000, true);
}

fn https_put(url: String, body: String, content_type: String) -> String {
    let headers = concat("Content-Type: ", content_type);
    return https_request("PUT", url, body, headers, 30000, true);
}

fn https_delete(url: String) -> String {
    return https_request("DELETE", url, "", "", 30000, true);
}

fn https_head(url: String) -> String {
    return https_request("HEAD", url, "", "", 30000, true);
}

fn https_ok() -> bool {
    let status = https_status();
    return status >= 200 && status < 300 && len(https_error()) == 0;
}

async fn https_get_async(url: String) -> String {
    return https_get(url);
}

async fn https_post_async(url: String, body: String, content_type: String) -> String {
    return https_post(url, body, content_type);
}
