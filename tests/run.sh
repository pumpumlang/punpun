#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Canonical acceptance gate. `make test` covers the compiler, the runtime,
# the first-party packages and the repository hygiene checks; the steps below
# add the slower self-host, ABI, compatibility, stress and fuzz gates.
make -j2 test
./selfhost/bootstrap.sh
python3 scripts/abi_check.py --ppc ./build/ppc
python3 scripts/compat_matrix.py --ppc ./build/ppc
python3 scripts/stress.py --quick --ppc ./build/ppc
python3 scripts/fuzz_frontend.py --iterations 100 --seed 20560 --ppc ./build/ppc
python3 scripts/check_links.py README.md docs compiler/docs spec examples editors/vscode/README.md CONTRIBUTING.md SECURITY.md
python3 scripts/privacy_audit.py .
