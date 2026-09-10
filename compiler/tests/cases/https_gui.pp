import std.net.https
import std.gui

launch {
    let rejected = https_request("GET", "http://example.com", "", "", 1000, true);
    say(rejected == "");
    say(https_status());
    say(contains(https_error(), "https://"));
    say(https_ok());
    say(gui_available() == gui_available());
}
