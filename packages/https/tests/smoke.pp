import src.main

launch {
    let body = https_get("http://not-allowed.example");
    assert(len(body) == 0, "plain HTTP must return an empty body");
    assert(https_status() == 0, "a rejected request has no status");
    assert(len(https_error()) > 0, "a rejected request must report an error");
    say("https-ok");
}
