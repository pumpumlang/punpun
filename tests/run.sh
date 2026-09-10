#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Canonical 1.3 gate. The historical Python tests target the replaced
# header-only compiler and remain as migration evidence, not as PPC acceptance.
make -j2 test
./selfhost/bootstrap.sh
python3 scripts/abi_check.py --ppc ./build/ppc
python3 scripts/compat_matrix.py --ppc ./build/ppc
python3 scripts/stress.py --quick --ppc ./build/ppc
python3 scripts/fuzz_frontend.py --iterations 100 --seed 20560 --ppc ./build/ppc
python3 scripts/check_links.py README.md docs docs-site/content examples editors/vscode/README.md CONTRIBUTING.md SECURITY.md
python3 scripts/privacy_audit.py .
