// ppc — the PunPun compiler.
//
// Entry point. All the work happens in Driver; this only turns argv into
// Options and maps failures onto a process exit status.
#include <cstdio>
#include <string>

#include "ppc/driver/driver.hpp"
#include "ppc/driver/options.hpp"

int main(int argc, char **argv) {
    ppc::Options options;
    std::string error;

    if (!ppc::parse_options(argc, argv, options, error)) {
        std::fprintf(stderr, "ppc: %s\n", error.c_str());
        return 2;
    }

    ppc::Driver driver(options);
    return driver.run();
}
