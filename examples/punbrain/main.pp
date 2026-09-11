@inject->c("""
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int64_t pp_arg_count(void);
extern const char *pp_arg(int64_t index);

static int pb_endpoint_allowed(const char *name) {
    static const char *allowed[] = {
        "stronghold",
        "all-advancements",
        "blind",
        "divine",
        "boat",
        "information-messages",
        "version",
        "ping"
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        if (strcmp(name, allowed[i]) == 0) return 1;
    }
    return 0;
}

static void pb_help(void) {
    puts("PunBrain Legal Relay");
    puts("Mirrors Ninjabrain Bot API data without additional calculations.");
    puts("Usage:");
    puts("  punbrain-legal stronghold");
    puts("  punbrain-legal all-advancements");
    puts("  punbrain-legal blind");
    puts("  punbrain-legal divine");
    puts("  punbrain-legal boat");
    puts("  punbrain-legal information-messages");
    puts("  punbrain-legal version");
    puts("  punbrain-legal ping");
    puts("Ninjabrain Bot API must be enabled on 127.0.0.1:52533.");
}

static int pb_run_curl(const char *endpoint) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "curl.exe --silent --show-error --fail --max-time 2 http://127.0.0.1:52533/api/v1/%s",
             endpoint);
    FILE *pipe = _popen(command, "r");
#else
    snprintf(command, sizeof(command),
             "curl --silent --show-error --fail --max-time 2 http://127.0.0.1:52533/api/v1/%s",
             endpoint);
    FILE *pipe = popen(command, "r");
#endif
    if (!pipe) {
        fputs("punbrain: could not start curl\n", stderr);
        return 2;
    }

    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        fwrite(buffer, 1, n, stdout);
    }
    fputc('\n', stdout);

#ifdef _WIN32
    return _pclose(pipe) == 0 ? 0 : 3;
#else
    return pclose(pipe) == 0 ? 0 : 3;
#endif
}

int64_t punbrain_legal_cli(void) {
    const int argc = (int)pp_arg_count();
    if (argc == 0) {
        pb_help();
        return 0;
    }

    const char *mode = pp_arg(0);
    if (strcmp(mode, "--version") == 0 || strcmp(mode, "legal-version") == 0) {
        puts("PunBrain Legal Relay 0.2.0");
        return 0;
    }

    if (!pb_endpoint_allowed(mode)) {
        fputs("punbrain: endpoint is not on the legal relay allow-list\n", stderr);
        pb_help();
        return 2;
    }

    return pb_run_curl(mode);
}
""");

extern native fn punbrain_legal_cli() -> i64;

launch {
    unsafe {
        let status = punbrain_legal_cli();
    }
}
