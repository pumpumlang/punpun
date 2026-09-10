import src.main

launch {
    let response = requests_get("http://not-allowed.example");
    assert(response.status == 0, "plain HTTP must not run");
    assert(len(response.error) > 0, "plain HTTP must report an error");
    assert(!response.ok(), "a rejected request cannot be successful");
    say("requests-ok");
}
