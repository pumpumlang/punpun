CXX ?= c++
CC ?= cc
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
CFLAGS ?= -std=c17 -O2 -Wall -Wextra -Wpedantic -Werror
BUILD := build
VERSION := $(strip $(shell tr -d '\r\n' < VERSION))

.PHONY: all compiler selfhost clean test version-sync version-check install install-vscode

# The core compiler is intentionally bootstrap-able with only C/C++ + make.
all: compiler

compiler: $(BUILD)/ppc

version-sync:
	python3 scripts/sync_version.py

version-check: compiler
	python3 scripts/check_version.py

selfhost: compiler runtime/libpunpun.a
	./selfhost/bootstrap.sh

$(BUILD):
	mkdir -p $(BUILD)

runtime/punpun.o: runtime/punpun.c runtime/punpun.h
	$(CC) $(CFLAGS) -c $< -o $@

runtime/libpunpun.a: runtime/punpun.o
	ar rcs $@ $^

PPC_SOURCES := compiler/main.cpp compiler/frontend.hpp compiler/diagnostics.hpp compiler/debug_dump.hpp compiler/semantic.hpp compiler/formatter.hpp compiler/process.hpp compiler/hir.hpp compiler/hir_opt.hpp compiler/mir.hpp compiler/builtins.hpp compiler/backend_c.hpp compiler/backend_x86_64.hpp

$(BUILD)/ppc: $(PPC_SOURCES) VERSION runtime/libpunpun.a | $(BUILD)
	$(CXX) $(CXXFLAGS) -DPP_RUNTIME_DIR='"runtime"' -DPP_VERSION='"$(VERSION)"' compiler/main.cpp -o $@

test: version-sync compiler
	python3 scripts/check_version.py
	./tests/run.sh
	./build/ppc check main.pp
	python3 -m json.tool editors/vscode/package.json >/dev/null
	python3 -m json.tool editors/vscode/snippets.json >/dev/null
	@if command -v node >/dev/null 2>&1; then node --check editors/vscode/extension.js; fi

install: compiler
	install -Dm755 $(BUILD)/ppc $(DESTDIR)/usr/local/bin/ppc
	install -Dm755 punpun $(DESTDIR)/usr/local/bin/punpun
	install -Dm755 pp $(DESTDIR)/usr/local/bin/pp
	mkdir -p $(DESTDIR)/usr/local/lib/punpun
	cp runtime/punpun.h runtime/libpunpun.a runtime/punpun.c $(DESTDIR)/usr/local/lib/punpun/
	mkdir -p $(DESTDIR)/usr/local/lib/punpun/stdlib
	cp -R stdlib/. $(DESTDIR)/usr/local/lib/punpun/stdlib/

install-vscode:
	./punpun editor install-vscode

clean:
	rm -rf $(BUILD) runtime/*.o runtime/*.a tests/tmp .punpun
