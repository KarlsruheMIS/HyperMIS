#!/usr/bin/env bash
# Build HyperMIS. Usage: ./build.sh [path/to/gurobi/linux64]
# The Gurobi path can also come from $GUROBI_HOME; otherwise CMake auto-detects /opt/gurobi*.
set -euo pipefail
cd "$(dirname "$0")"

GUROBI_HOME="${1:-${GUROBI_HOME:-}}"
CMAKE_ARGS=()

if [[ -n "$GUROBI_HOME" ]]; then
    if [[ ! -f "$GUROBI_HOME/include/gurobi_c++.h" ]]; then
        echo "error: '$GUROBI_HOME' is not a Gurobi installation (no include/gurobi_c++.h)." >&2
        echo "       Point it at the directory containing include/ and lib/, e.g. /opt/gurobi1203/linux64" >&2
        exit 1
    fi
    CMAKE_ARGS+=(-DGUROBI_HOME="$GUROBI_HOME")
fi

# License check (needed at runtime).
license=""
for f in "${GRB_LICENSE_FILE:-}" "$HOME/gurobi.lic" /opt/gurobi/gurobi.lic; do
    if [[ -n "$f" && -f "$f" ]]; then license="$f"; break; fi
done
if [[ -n "$license" ]]; then
    echo "Gurobi license found: $license"
else
    echo "warning: no Gurobi license found (checked \$GRB_LICENSE_FILE, ~/gurobi.lic, /opt/gurobi/gurobi.lic)." >&2
    echo "         Building anyway, but the ILP solvers will fail at runtime." >&2
    echo "         Get one at https://www.gurobi.com and set: export GRB_LICENSE_FILE=/path/to/gurobi.lic" >&2
fi

mkdir -p build
cmake -S . -B build "${CMAKE_ARGS[@]}"
cmake --build build -j "$(nproc)"
echo "Done. Executables are in build/"
