#!/bin/bash
#
# Clone, patch and build every competing solver the experiments compare against,
# each pinned to the exact commit that produced the numbers in the paper.
#
#   experiments/setup_competitors.sh                 install everything
#   experiments/setup_competitors.sh --list          print the pinned commits and exit
#   experiments/setup_competitors.sh --only struction
#   experiments/setup_competitors.sh --skip bmatching --skip hypermis
#   experiments/setup_competitors.sh --prefix ~/solvers
#   experiments/setup_competitors.sh --no-build      clone + patch only
#   experiments/setup_competitors.sh --force         re-checkout even if the tree is dirty
#   experiments/setup_competitors.sh -j 16
#
# COMPONENTS
#   struction   KaMIS               -> KaMIS/deploy/struction              ($STRUCTION)
#   vc_solver   WeGotYouCovered     -> WeGotYouCovered/optimized/vc_solver ($VC)
#   satreduce   vc-satreduce+CaDiCaL-> vc-satreduce/build/vc-bnb           ($SATREDUCE)
#   bmatching   Bmatching           -> Bmatching/build/app/bmatching_cli   ($BM)
#   hypermis    this repo           -> build/{run_reduce,run_ilp,...}
#
# The default prefix is the PARENT of this repository, which is exactly where
# experiments/config.sh looks for the four external binaries -- install there and
# nothing needs to be configured. With any other --prefix the script prints the
# export lines to use instead.
#
# Everything is idempotent: an existing clone is fetched and re-checked-out at
# the pin, an already-applied patch is skipped, and a component whose binary is
# already in place and up to date is left alone.
#
# GUROBI is commercial and cannot be installed from here. It is required by
# `hypermis` (run_ilp, graph_reduction_comparison) and by `bmatching`; the other
# components build without it. Get a licence (academic licences are free), then
# set GUROBI_HOME.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$HERE/.." && pwd)"
PATCHES="$HERE/patches"

# ---------------------------------------------------------------------------
# The pins -- the commits these numbers were produced with, so a rerun gets the
# same solvers and not whatever the default branches have become since.
#
# One has a consequence worth knowing: vc-satreduce 7ea7b3b aborts on an assertion
# for 6 of our instances (an upstream totalizer-sizing bug), which the paper
# reports as a crash. Nothing here suppresses that -- see install_satreduce.
#
# WeGotYouCovered has no public repository of its own; its source is the PACE
# 2019 submission repo (KarlsruheMIS/pace-2019, mirrored at sebalamm/pace-2019),
# which is why it is cloned from there into a directory named WeGotYouCovered.
# ---------------------------------------------------------------------------
KAMIS_URL="https://github.com/KarlsruheMIS/KaMIS.git"
KAMIS_COMMIT="0a786688702b46391625748451270a6ec46f9293"        # v3.0-12-g0a78668
KAHIP_COMMIT="b6bedee5fdef49108ad8400389497192b6b23f64"        # mmwis/extern/KaHIP submodule

WGYC_URL="https://github.com/KarlsruheMIS/pace-2019.git"
WGYC_COMMIT="5184adaba64b0f499043ec33a02b47891a895a61"         # master tip

SATREDUCE_URL="https://github.com/avdgrinten/vc-satreduce.git"
SATREDUCE_COMMIT="7ea7b3b79daa4c6952c4d88607303d1e91c1321d"

CADICAL_URL="https://github.com/arminbiere/cadical.git"
CADICAL_COMMIT="14593f819cb242082b127396218bd60889409737"      # rel-2.2.0

BM_URL="https://github.com/HeiHGM/Bmatching.git"
BM_COMMIT="2f456c32c1d28de28b02822d48e383d769e667ea"           # v1.0.0-10-g2f456c3

ALL=(struction vc_solver satreduce bmatching hypermis)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
PREFIX="$(cd "$REPO_DIR/.." && pwd)"
JOBS="$(nproc 2>/dev/null || echo 4)"
NO_BUILD=0 FORCE=0 LIST=0
declare -a ONLY=() SKIP=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)   PREFIX="$2"; shift ;;
    --only)     ONLY+=("$2"); shift ;;
    --skip)     SKIP+=("$2"); shift ;;
    --jobs|-j)  JOBS="$2"; shift ;;
    --no-build) NO_BUILD=1 ;;
    --force)    FORCE=1 ;;
    --list)     LIST=1 ;;
    -h|--help)  grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
  esac
  shift
done

if ((LIST)); then
  printf '%-12s %-46s %s\n' COMPONENT REPOSITORY COMMIT
  printf '%-12s %-46s %s\n' struction "$KAMIS_URL" "$KAMIS_COMMIT"
  printf '%-12s %-46s %s\n' ""        "  submodule mmwis/extern/KaHIP" "$KAHIP_COMMIT"
  printf '%-12s %-46s %s\n' vc_solver "$WGYC_URL" "$WGYC_COMMIT"
  printf '%-12s %-46s %s\n' satreduce "$SATREDUCE_URL" "$SATREDUCE_COMMIT"
  printf '%-12s %-46s %s\n' ""        "$CADICAL_URL" "$CADICAL_COMMIT"
  printf '%-12s %-46s %s\n' bmatching "$BM_URL" "$BM_COMMIT"
  exit 0
fi

selected=("${ALL[@]}")
if ((${#ONLY[@]})); then selected=("${ONLY[@]}"); fi
declare -a COMPONENTS=()
for c in "${selected[@]}"; do
  known=0; for k in "${ALL[@]}"; do [[ "$c" == "$k" ]] && known=1; done
  ((known)) || { echo "unknown component: $c (one of: ${ALL[*]})" >&2; exit 2; }
  skipit=0; for s in "${SKIP[@]:-}"; do [[ "$c" == "$s" ]] && skipit=1; done
  ((skipit)) || COMPONENTS+=("$c")
done
((${#COMPONENTS[@]})) || { echo "nothing to do."; exit 0; }

mkdir -p "$PREFIX" || exit 1
PREFIX="$(cd "$PREFIX" && pwd)"

# ---------------------------------------------------------------------------
# Output helpers + a summary that survives a partial failure. One component
# failing must not abort the rest: a reviewer without a Gurobi licence should
# still end up with the three solvers that do not need one, and be told exactly
# which one did not build and why -- not be left with a dead script and no
# summary at all.
# ---------------------------------------------------------------------------
declare -A STATUS=() DETAIL=()
say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '\033[33m    WARNING %s\033[0m\n' "$*" >&2; }
err()  { printf '\033[31m    ERROR   %s\033[0m\n' "$*" >&2; }
ok()   { STATUS[$1]=ok;      DETAIL[$1]="${2:-}"; }
fail() { STATUS[$1]=FAILED;  DETAIL[$1]="${2:-}"; err "$1: ${2:-failed}"; }

# Skip the build when the binary is already there. Reaching this point means the
# clone is at the pin (checkout ran first), so the binary was built from the
# right source; rebuilding it would only cost minutes. --force rebuilds anyway.
built() {  # built COMPONENT BINARY
  ((FORCE)) && return 1
  [[ -x "$2" ]] || return 1
  info "$2 already built -- skipping (use --force to rebuild)"
  ok "$1" "$2"
  return 0
}

run() {  # run a build step, appending to a log; on failure show the tail
  local log="$1"; shift
  # append, never truncate: a component's steps share one log (configure, then
  # build), and the interesting error is often in the earlier step.
  printf '\n===== %s =====\n' "$*" >>"$log"
  if "$@" >>"$log" 2>&1; then return 0; fi
  err "command failed: $*"
  err "last 15 lines of $log:"
  tail -15 "$log" | sed 's/^/        /' >&2
  return 1
}

# ---------------------------------------------------------------------------
# checkout URL DIR COMMIT [--recursive]
#
# Clone if absent, otherwise fetch and move to the pin. A dirty tree is NEVER
# silently reset: local edits here are usually the build patches below (or a
# reviewer's own debugging), and throwing them away without asking is the one
# thing an install script must not do. --force overrides.
# ---------------------------------------------------------------------------
checkout() {
  local url="$1" dir="$2" commit="$3" recursive="${4:-}"
  if [[ -d "$dir/.git" ]]; then
    local at; at="$(git -C "$dir" rev-parse HEAD 2>/dev/null)"
    if [[ "$at" == "$commit" ]]; then
      info "$(basename "$dir"): already at ${commit:0:9}"
    else
      if [[ -n "$(git -C "$dir" status --porcelain --untracked-files=no 2>/dev/null)" ]] && ((!FORCE)); then
        warn "$(basename "$dir") has uncommitted changes and is at ${at:0:9}, not ${commit:0:9}."
        warn "leaving it alone -- commit/stash them, or re-run with --force."
        return 2   # distinct from a real failure: the caller reports it as SKIPPED
      fi
      info "$(basename "$dir"): fetching ${commit:0:9}"
      git -C "$dir" fetch --quiet origin "$commit" 2>/dev/null || git -C "$dir" fetch --quiet --tags origin || true
      git -C "$dir" checkout --quiet --force "$commit" || return 1
    fi
  else
    info "cloning $(basename "$dir") from $url"
    git clone --quiet "$url" "$dir" || return 1
    git -C "$dir" checkout --quiet --detach "$commit" || return 1
  fi
  if [[ "$recursive" == "--recursive" ]]; then
    git -C "$dir" submodule update --quiet --init --recursive || return 1
  fi
  return 0
}

# prepare COMPONENT URL DIR COMMIT [--recursive]
#
# checkout(), with the outcome turned into this component's status. A tree that
# was left alone because it is dirty is NOT a success -- whatever gets built from
# it is not the pinned code, and the summary has to say so -- but it is not a
# failure either, so it does not fail the script.
prepare() {
  local comp="$1"; shift
  checkout "$@"
  case $? in
    0) return 0 ;;
    2) STATUS[$comp]=SKIPPED
       DETAIL[$comp]="not at the pinned commit (local changes kept; --force to reset)"
       return 1 ;;
    *) fail "$comp" "checkout of $(basename "$2") failed"; return 1 ;;
  esac
}

# apply_patch REPO PATCHFILE [STRIP] -- skip if it is already in the tree
apply_patch() {
  local dir="$1" patch="$2" strip="${3:-1}"
  [[ -f "$patch" ]] || { err "missing patch file $patch"; return 1; }
  if git -C "$dir" apply -R --check -p"$strip" "$patch" 2>/dev/null; then
    info "patch $(basename "$patch"): already applied"
    return 0
  fi
  if ! git -C "$dir" apply --check -p"$strip" "$patch" 2>/dev/null; then
    err "patch $(basename "$patch") does not apply to $dir (wrong commit?)"
    return 1
  fi
  git -C "$dir" apply -p"$strip" "$patch" || return 1
  info "patch $(basename "$patch"): applied"
}

# ---------------------------------------------------------------------------
# Prerequisites. Checked once, up front, per selected component -- a missing
# header is far cheaper to report now than 40 build-log lines later.
# ---------------------------------------------------------------------------
need_cmd() { command -v "$1" >/dev/null 2>&1; }

# scons (vc_solver) is not packaged on many systems and, when it is, PEP-668
# blocks `pip install --user`. Rather than touch the reviewer's Python
# environment we drop a throwaway venv next to the clones and put it on PATH for
# this script only.
TOOLVENV="$PREFIX/.hypermis-buildtools"
ensure_pytool() {  # ensure_pytool COMMAND PIPPKG
  need_cmd "$1" && return 0
  if [[ ! -x "$TOOLVENV/bin/$1" ]]; then
    info "$1 not found -- installing it into $TOOLVENV"
    python3 -m venv "$TOOLVENV" >/dev/null 2>&1 || { err "python3 -m venv failed (install python3-venv)"; return 1; }
    "$TOOLVENV/bin/pip" install --quiet --upgrade pip >/dev/null 2>&1
    "$TOOLVENV/bin/pip" install --quiet "$2" || { err "pip install $2 failed (no network?)"; return 1; }
  fi
  PATH="$TOOLVENV/bin:$PATH"; export PATH
  need_cmd "$1"
}

missing=()
for c in git cmake make g++; do need_cmd "$c" || missing+=("$c"); done
if ((${#missing[@]})); then
  err "required tools not found: ${missing[*]}"
  exit 1
fi

# ---------------------------------------------------------------------------
# struction -- KaMIS
#
# compile_withcmake.sh builds all of KaMIS (redumis, mmwis, online_mis, ...) and
# takes many minutes. We need exactly one binary, so we configure mmwis and build
# only its struction target, then place it where config.sh expects it. The copy
# to deploy/ mirrors what compile_withcmake.sh does, so a full upstream build
# would land in the same place.
# ---------------------------------------------------------------------------
install_struction() {
  local dir="$PREFIX/KaMIS"
  say "struction (KaMIS)"
  prepare struction "$KAMIS_URL" "$dir" "$KAMIS_COMMIT" --recursive || return
  local sub; sub="$(git -C "$dir" submodule status mmwis/extern/KaHIP 2>/dev/null | awk '{print $1}' | tr -d '+-')"
  [[ -z "$sub" || "$sub" == "$KAHIP_COMMIT" ]] || warn "KaHIP submodule is at ${sub:0:9}, expected ${KAHIP_COMMIT:0:9}"
  ((NO_BUILD)) && { ok struction "cloned (build skipped)"; return; }
  built struction "$dir/deploy/struction" && return

  local bld="$dir/mmwis/build" log="$dir/mmwis/build.log"
  info "building target branch_reduce_convergence (-j $JOBS), log: $log"
  run "$log" cmake -S "$dir/mmwis" -B "$bld" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$(command -v gcc)" -DCMAKE_CXX_COMPILER="$(command -v g++)" \
      || { fail struction "cmake configure failed"; return; }
  run "$log" cmake --build "$bld" --target branch_reduce_convergence -j "$JOBS" \
      || { fail struction "build failed"; return; }

  mkdir -p "$dir/deploy"
  cp "$bld/extern/struction/branch_reduce_convergence" "$dir/deploy/struction" \
      || { fail struction "built binary not found"; return; }
  ok struction "$dir/deploy/struction"
}

# ---------------------------------------------------------------------------
# vc_solver -- WeGotYouCovered (PACE 2019)
#
# Python-2 SConstruct and a link line that wants a static argtable2 that the tree
# does not ship; both are fixed by patches/wegotyoucovered-build.patch. The
# linker-name symlink is missing from the distribution too and cannot live in a
# patch, so it is created here.
# ---------------------------------------------------------------------------
install_vc_solver() {
  local dir="$PREFIX/WeGotYouCovered"
  say "vc_solver (WeGotYouCovered / PACE 2019)"
  prepare vc_solver "$WGYC_URL" "$dir" "$WGYC_COMMIT" || return
  apply_patch "$dir" "$PATCHES/wegotyoucovered-build.patch" || { fail vc_solver "patch failed"; return; }

  local libdir="$dir/extern/argtable-2.10/lib"
  if [[ -f "$libdir/libargtable2.so.0.1.5" && ! -e "$libdir/libargtable2.so" ]]; then
    ln -sf libargtable2.so.0.1.5 "$libdir/libargtable2.so"
    info "created linker-name symlink libargtable2.so"
  fi
  ((NO_BUILD)) && { ok vc_solver "cloned + patched (build skipped)"; return; }
  built vc_solver "$dir/optimized/vc_solver" && return

  ensure_pytool scons scons || { fail vc_solver "scons unavailable"; return; }
  local log="$dir/build.log"
  info "building with scons (-j $JOBS), log: $log"
  ( cd "$dir" && run "$log" scons program=vc_solver variant=optimized -j "$JOBS" ) \
      || { fail vc_solver "scons build failed"; return; }
  [[ -x "$dir/optimized/vc_solver" ]] || { fail vc_solver "vc_solver not produced"; return; }
  ok vc_solver "$dir/optimized/vc_solver"
}

# ---------------------------------------------------------------------------
# satreduce -- vc-satreduce, plus CaDiCaL, which it finds only via pkg-config.
#
# Upstream ships no cadical.pc; the README gives a template, and we write it into
# the clone pointing at our CaDiCaL build.
#
# The build is the one upstream's README documents, `--buildtype=debugoptimized`,
# with nothing overridden. Worth knowing what that leaves in place: meson's
# b_ndebug defaults to false, so asserts stay compiled in, and vc-bnb aborts on
# one for 6 of our instances (an upstream totalizer bug). That is the behaviour
# the paper reports as a crash -- an assert-free build would not fix those
# instances, it would return covers that may be suboptimal without saying so.
#
# The compiler is the machine's own default. It is only overridden when that
# default demonstrably cannot link the system Boost -- a newer GCC installed under
# /usr/local and picked up from PATH may miss the GLIBCXX version Boost was built
# against, which stops the build outright. Set CXX to choose one yourself.
# ---------------------------------------------------------------------------
install_satreduce() {
  local dir="$PREFIX/vc-satreduce" cad="$PREFIX/cadical"
  say "satreduce (vc-satreduce + CaDiCaL)"
  prepare satreduce "$CADICAL_URL" "$cad" "$CADICAL_COMMIT" || return
  prepare satreduce "$SATREDUCE_URL" "$dir" "$SATREDUCE_COMMIT" || return
  ((NO_BUILD)) && { ok satreduce "cloned (build skipped)"; return; }
  built satreduce "$dir/build/vc-bnb" && return

  if [[ ! -f "$cad/build/libcadical.a" ]]; then
    local clog="$cad/build.log"
    info "building CaDiCaL, log: $clog"
    ( cd "$cad" && run "$clog" ./configure ) || { fail satreduce "cadical configure failed"; return; }
    ( cd "$cad" && run "$clog" make -j "$JOBS" ) || { fail satreduce "cadical build failed"; return; }
  else
    info "cadical: libcadical.a already built"
  fi

  mkdir -p "$dir/pkgconfig"
  cat > "$dir/pkgconfig/cadical.pc" <<EOF
prefix=$cad

Name: cadical
Version: 1.0.3
Description: CaDiCaL
Cflags: -I\${prefix}/src
Libs: -L\${prefix}/build -lcadical
EOF

  need_cmd pkg-config || { fail satreduce "pkg-config not installed"; return; }
  ensure_pytool meson meson || { fail satreduce "meson unavailable"; return; }
  ensure_pytool ninja ninja || { fail satreduce "ninja unavailable"; return; }
  if ! echo '#include <boost/program_options.hpp>' | g++ -x c++ -fsyntax-only - 2>/dev/null; then
    fail satreduce "Boost headers not found (need libboost-program-options-dev, libboost-system-dev)"; return
  fi

  # Probe the default compiler by actually linking against Boost -- the failure
  # mode is a missing GLIBCXX at link time, which no version check would catch.
  local -a envcxx=()
  if [[ -n "${CXX:-}" ]]; then
    envcxx=(env CXX="$CXX"); info "using CXX=$CXX from the environment"
  elif ! echo 'int main(){}' | "$(command -v c++ || command -v g++)" -x c++ - \
         -lboost_program_options -o /dev/null 2>/dev/null; then
    local fallback=/usr/bin/g++
    [[ -x "$fallback" ]] || fallback="$(command -v g++)"
    envcxx=(env CXX="$fallback")
    warn "the default c++ cannot link Boost -- building with $fallback instead"
  fi

  local bld="$dir/build" log="$dir/build.log"
  info "meson setup (buildtype=debugoptimized, as upstream documents), log: $log"
  rm -rf "$bld"
  run "$log" "${envcxx[@]}" meson setup "$bld" "$dir" --buildtype=debugoptimized \
        --pkg-config-path="$dir/pkgconfig" || { fail satreduce "meson setup failed"; return; }
  run "$log" ninja -C "$bld" || { fail satreduce "ninja build failed"; return; }
  [[ -x "$bld/vc-bnb" ]] || { fail satreduce "vc-bnb not produced"; return; }
  ok satreduce "$bld/vc-bnb"
}

# ---------------------------------------------------------------------------
# bmatching -- HeiHGM/Bmatching, the b-matching SOTA the duality comparison uses.
#
# Needs Gurobi (that is the point: the comparison runs its exact ILP), plus
# ncurses headers. Where ncurses is only in a Homebrew/linuxbrew prefix, CMake
# will not find it on its own, so we hand it the paths. All other dependencies
# are fetched by CMake FetchContent, which needs network during configure.
# ---------------------------------------------------------------------------
install_bmatching() {
  local dir="$PREFIX/Bmatching"
  say "bmatching (HeiHGM/Bmatching)"
  prepare bmatching "$BM_URL" "$dir" "$BM_COMMIT" || return
  apply_patch "$dir" "$PATCHES/bmatching-gcc12-std-max.patch" || { fail bmatching "patch failed"; return; }
  ((NO_BUILD)) && { ok bmatching "cloned + patched (build skipped)"; return; }
  built bmatching "$dir/build/app/bmatching_cli" && return

  if [[ -z "${GUROBI_HOME:-}" || ! -d "${GUROBI_HOME:-}" ]]; then
    fail bmatching "GUROBI_HOME is not set to an existing directory -- Bmatching's ILP needs it"
    return
  fi

  local -a extra=()
  if ! echo '#include <ncurses.h>' | g++ -x c++ -fsyntax-only - 2>/dev/null; then
    local brew=""
    for p in /home/linuxbrew/.linuxbrew /opt/homebrew /usr/local; do
      [[ -f "$p/lib/libncurses.so" || -f "$p/lib/libncurses.dylib" ]] && { brew="$p"; break; }
    done
    if [[ -n "$brew" ]]; then
      info "ncurses headers not in the default include path -- using $brew"
      extra+=(-DCMAKE_PREFIX_PATH="$brew" -DCURSES_INCLUDE_PATH="$brew/include"
              -DCURSES_LIBRARY="$brew/lib/libncurses.so")
    else
      warn "ncurses dev headers not found; install libncurses-dev if the build fails"
    fi
  fi

  local bld="$dir/build" log="$dir/build.log"
  info "cmake configure (Gurobi ON, fetches dependencies), log: $log"
  run "$log" cmake -S "$dir" -B "$bld" -DCMAKE_BUILD_TYPE=Release \
        -DBMATCHING_USE_GUROBI=ON -DBUILD_TESTING=OFF "${extra[@]}" \
      || { fail bmatching "cmake configure failed"; return; }
  info "building bmatching_cli (-j $JOBS)"
  run "$log" cmake --build "$bld" --target bmatching_cli -j "$JOBS" \
      || { fail bmatching "build failed"; return; }
  [[ -x "$bld/app/bmatching_cli" ]] || { fail bmatching "bmatching_cli not produced"; return; }
  ok bmatching "$bld/app/bmatching_cli"
}

# ---------------------------------------------------------------------------
# hypermis -- this repository's own binaries.
#
# GUROBI_HOME is set with a plain set() in CMakeLists.txt, so -D on the command
# line does NOT override it. Rewriting a reviewer's CMakeLists is not this
# script's job, so we check the value and say exactly which line to edit.
# ---------------------------------------------------------------------------
install_hypermis() {
  say "hypermis (this repository)"
  local cml="$REPO_DIR/CMakeLists.txt"
  local pinned; pinned="$(awk '/^[[:space:]]*set\(GUROBI_HOME/ {print $2}' "$cml" | tr -d ')')"
  if [[ -n "$pinned" && ! -d "$pinned" ]]; then
    err "CMakeLists.txt line $(grep -n 'set(GUROBI_HOME' "$cml" | cut -d: -f1) points at"
    err "  $pinned"
    err "which does not exist here. Edit it to your Gurobi install${GUROBI_HOME:+ (GUROBI_HOME=$GUROBI_HOME)}, then re-run."
    fail hypermis "GUROBI_HOME in CMakeLists.txt does not exist"
    return
  fi
  ((NO_BUILD)) && { ok hypermis "build skipped"; return; }

  local bld="${BUILD:-$REPO_DIR/build}" log
  mkdir -p "$bld"; log="$bld/setup.log"   # inside build/, never a stray file in the repo root
  info "building into $bld (-j $JOBS), log: $log"
  run "$log" cmake -S "$REPO_DIR" -B "$bld" -DCMAKE_BUILD_TYPE=Release \
      || { fail hypermis "cmake configure failed"; return; }
  run "$log" cmake --build "$bld" -j "$JOBS" || { fail hypermis "build failed"; return; }
  local miss=()
  for b in run_reduce run_ilp hypergraph_to_graph clique_blowup graph_reduction_comparison; do
    [[ -x "$bld/$b" ]] || miss+=("$b")
  done
  ((${#miss[@]})) && { fail hypermis "missing binaries: ${miss[*]}"; return; }
  ok hypermis "$bld"
}

# ---------------------------------------------------------------------------
say "installing into $PREFIX  (components: ${COMPONENTS[*]})"
[[ "$PREFIX" == "$(cd "$REPO_DIR/.." && pwd)" ]] \
  || info "note: not the default prefix -- export lines are printed at the end"

for c in "${COMPONENTS[@]}"; do
  case "$c" in
    struction) install_struction ;;
    vc_solver) install_vc_solver ;;
    satreduce) install_satreduce ;;
    bmatching) install_bmatching ;;
    hypermis)  install_hypermis ;;
  esac
done

# ---------------------------------------------------------------------------
# Summary + the exact configuration the pipeline needs.
# ---------------------------------------------------------------------------
declare -A VARPATH=(
  [struction]="$PREFIX/KaMIS/deploy/struction"
  [vc_solver]="$PREFIX/WeGotYouCovered/optimized/vc_solver"
  [satreduce]="$PREFIX/vc-satreduce/build/vc-bnb"
  [bmatching]="$PREFIX/Bmatching/build/app/bmatching_cli"
)
declare -A VARNAME=([struction]=STRUCTION [vc_solver]=VC [satreduce]=SATREDUCE [bmatching]=BM)

say "summary"
rc=0
for c in "${COMPONENTS[@]}"; do
  s="${STATUS[$c]:-FAILED}"
  [[ "$s" == ok ]] || rc=1
  printf '    %-10s %-8s %s\n' "$c" "$s" "${DETAIL[$c]:-}"
done

exports=()
for c in "${COMPONENTS[@]}"; do
  [[ -n "${VARNAME[$c]:-}" ]] || continue
  [[ "${STATUS[$c]:-}" == ok ]] || continue
  exports+=("export ${VARNAME[$c]}=${VARPATH[$c]}")
done
if ((${#exports[@]})) && [[ "$PREFIX" != "$(cd "$REPO_DIR/.." && pwd)" ]]; then
  say "point the pipeline at this prefix"
  printf '    %s\n' "${exports[@]}"
  info "(or put them in experiments/config.sh)"
elif ((${#exports[@]})); then
  say "nothing to configure -- experiments/config.sh already looks here"
fi

info ""
info "next: experiments/run_all.sh --status"
exit $rc
