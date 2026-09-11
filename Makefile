CXX ?= c++
CC ?= cc
BUILD := build
VERSION := $(strip $(shell tr -d '\r\n' < VERSION))

CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Wno-unused-parameter
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Icompiler/include -Iruntime
LDLIBS += -lpthread -lm
ifeq ($(shell uname -s 2>/dev/null),Linux)
LDLIBS += -ldl
endif
ifeq ($(OS),Windows_NT)
LDLIBS += -luser32
endif

PPC_SOURCES := $(wildcard compiler/src/*.cpp) \
               $(wildcard compiler/src/support/*.cpp) \
               $(wildcard compiler/src/syntax/*.cpp) \
               $(wildcard compiler/src/sema/*.cpp) \
               $(wildcard compiler/src/hir/*.cpp) \
               $(wildcard compiler/src/mir/*.cpp) \
               $(wildcard compiler/src/codegen/*.cpp) \
               $(wildcard compiler/src/driver/*.cpp) \
               $(wildcard compiler/src/service/*.cpp)
RUNTIME_SOURCES := runtime/ppcrt.c runtime/ppc_https.c runtime/ppc_gui.c \
                   runtime/ppc_platform_posix.c runtime/ppc_platform_windows.c
PPC_OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(PPC_SOURCES))
RUNTIME_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(RUNTIME_SOURCES))
DEPS := $(PPC_OBJECTS:.o=.d) $(RUNTIME_OBJECTS:.o=.d)

.PHONY: all compiler clean test compiler-test package-test docs doctest fuzz compat stress stability release-policy version-sync version-check install install-vscode

all: compiler

compiler: version-sync $(BUILD)/ppc

$(BUILD)/ppc: $(PPC_OBJECTS) $(RUNTIME_OBJECTS)
	@echo "  link    $@"
	@$(CXX) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  c++     $<"
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  cc      $<"
	@$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -MMD -MP -c $< -o $@

-include $(DEPS)

version-sync:
	python3 scripts/sync_version.py

version-check: compiler
	python3 scripts/check_version.py

compiler-test: compiler
	python3 compiler/tests/run_tests.py --ppc ./build/ppc --backend c --backend bytecode --backend native
	PPC=./build/ppc python3 compiler/tests/lsp/test_lsp.py

package-test: compiler
	@for backend in c native bytecode; do \
		./build/ppc run --module-path packages/https --backend=$$backend packages/https/tests/smoke.pp | grep -Fx https-ok; \
		./build/ppc run --module-path packages/gui --backend=$$backend packages/gui/tests/smoke.pp | grep -Fx gui-ok; \
		./build/ppc run --module-path packages/requests --backend=$$backend packages/requests/tests/smoke.pp | grep -Fx requests-ok; \
	done

test: version-sync compiler compiler-test package-test
	python3 scripts/check_version.py
	./build/ppc check main.pp
	python3 scripts/docgen.py --check
	python3 scripts/doctest.py --ppc ./build/ppc
	python3 scripts/stability.py check --ppc ./build/ppc
	python3 scripts/platform_policy.py --check
	python3 tests/test_desktop_integration.py
	python3 tests/test_publish_cleanup.py
	python3 tests/test_release_hygiene.py
	python3 tests/test_toolchain_update.py
	python3 -m json.tool editors/vscode/package.json >/dev/null
	python3 -m json.tool editors/vscode/snippets.json >/dev/null
	@if command -v node >/dev/null 2>&1; then node --check editors/vscode/extension.js; fi

docs: compiler
	python3 scripts/docgen.py

doctest: compiler
	python3 scripts/doctest.py --ppc ./build/ppc

fuzz: compiler
	python3 scripts/fuzz_frontend.py --iterations 100 --ppc ./build/ppc

compat: compiler
	python3 scripts/compat_matrix.py --ppc ./build/ppc

stress: compiler
	python3 scripts/stress.py --quick --ppc ./build/ppc

stability: compiler
	python3 scripts/stability.py check --ppc ./build/ppc
	python3 scripts/abi_check.py --ppc ./build/ppc

release-policy: compiler
	python3 scripts/platform_policy.py --check
	python3 scripts/stability.py check --ppc ./build/ppc

install: compiler
	install -Dm755 $(BUILD)/ppc $(DESTDIR)/usr/local/bin/ppc
	install -Dm755 punpun $(DESTDIR)/usr/local/bin/punpun
	install -Dm755 pp $(DESTDIR)/usr/local/bin/pp
	mkdir -p $(DESTDIR)/usr/local/lib/punpun/runtime
	cp runtime/ppcrt.h runtime/ppcrt.c runtime/ppc_https.c runtime/ppc_gui.c runtime/ppc_platform.h runtime/ppc_platform_posix.c runtime/ppc_platform_windows.c $(DESTDIR)/usr/local/lib/punpun/runtime/
	mkdir -p $(DESTDIR)/usr/local/lib/punpun/stdlib
	cp -R stdlib/. $(DESTDIR)/usr/local/lib/punpun/stdlib/

install-vscode:
	./punpun editor install-vscode

clean:
	rm -rf $(BUILD) compiler/build compiler/build-debug compiler/ppc compiler/ppc-debug tests/tmp .punpun
