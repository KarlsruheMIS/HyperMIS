#!/bin/bash
# ---------------------------------------------------------------------------
# Central configuration for the experiments/ experiment pipeline.
#
# Every experiment script sources this file. Edit values HERE, not in the individual
# experiment scripts. Nothing about any particular machine is hard-coded -- set
# N_JOBS and MEM_LIMIT for whatever machine you actually run on.
# ---------------------------------------------------------------------------

# Resolve the repo root from this file's location, so the scripts work no matter
# what directory they are launched from.
EXP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$EXP_DIR/.." && pwd)"

# ---- Paths ----------------------------------------------------------------
# Override RES to write a run somewhere else (a scratch dir for a trial run, a
# second dir for a different instance set) without touching the real tables.
RES="${RES:-$REPO_DIR/results}"
FAILURES="$RES/FAILURES.tsv"

# Instance sets. The defaults are the hypergraphs shipped WITH the repo, so the
# pipeline runs out of the box on a fresh clone with nothing else installed.
#
#   HG_FULL      full set   -- the reduction blocks run on this
#   HG_SOLVABLE  subset     -- the exact solvers / ILP blocks run on this; built
#                             from HG_FULL by collect_ilp_solvable.sh (symlinks)
#
# Both are env-overridable, so a larger collection is one variable away:
#   HG_FULL=~/test_instances/hypergraphs \
#   HG_SOLVABLE=~/test_instances/hypergraphs_ilp_solvable experiments/run_all.sh
HG_FULL="${HG_FULL:-$REPO_DIR/hypergraphs}"
HG_SOLVABLE="${HG_SOLVABLE:-$REPO_DIR/hypergraphs_ilp_solvable}"

# A missing or empty set is a silent no-op otherwise: `mapfile < <(ls DIR/*)`
# yields nothing, every block reports "0/0 groups, nothing to do", and it looks
# like the experiment ran. Say so loudly instead. Not fatal -- status.sh must
# stay readable, and collect_ilp_solvable.sh legitimately runs before
# HG_SOLVABLE exists.
for _set in HG_FULL HG_SOLVABLE; do
  _dir="${!_set}"
  if [[ ! -d "$_dir" ]]; then
    echo "experiments: WARNING $_set=$_dir does not exist -- every block over it will do nothing." >&2
    [[ "$_set" == HG_SOLVABLE ]] && echo "experiments:          run experiments/collect_ilp_solvable.sh to build it." >&2
  elif [[ -z "$(ls -A "$_dir" 2>/dev/null)" ]]; then
    echo "experiments: WARNING $_set=$_dir is empty -- every block over it will do nothing." >&2
  fi
done
unset _set _dir

# ---- Experiment parameters ------------------------------------------------
SEEDS=(1 21 203 1002)

# run_reduce -r<c> configurations measured by config_stats: 1-7 single rules,
# 8 the full pipeline (compiled default), 9-15 full minus the k-th rule. Lives
# HERE (not in run_reduction_statistics.sh) so status.sh counts against the same
# list the runner uses -- otherwise adding a config makes status.sh silently
# report COMPLETE while the new configs have never been run.
CONFIGS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
T=3600             # solver / ILP wall budget (seconds)
K=3600             # kernel (reduction) budget (seconds)

# ---- CPU pinning (see topology.sh) ----------------------------------------
# Every parallel slot is bound to a fixed, disjoint set of physical cores inside
# ONE L3 domain (a CCX on AMD), so a job is never migrated across L3 domains and
# never shares a physical core with another job via SMT. Without this, identical
# deterministic runs differ by tens of percent purely from scheduler placement.
#
#   PIN_MODE=ccx   one job per L3 domain -- whole CCX each, best isolation
#                  (N_JOBS is capped at the number of domains: 16 here)
#   PIN_MODE=core  N_JOBS slots carved from the CCX-ordered physical cores:
#                  N_JOBS=32 -> 2 cores/job, 2 jobs per CCX (share L3, not a core)
#                  N_JOBS=64 -> 1 core/job
#   PIN_MODE=off   old behaviour, unpinned
#
# N_JOBS=0/auto means "one job per L3 domain". Throughput vs. cleanliness: `ccx`
# halves the concurrency of the old -j 32 but makes each timing trustworthy; use
# PIN_MODE=core N_JOBS=32 to keep the old width with core-exclusive slots.
PIN_MODE="${PIN_MODE:-ccx}"
N_JOBS="${N_JOBS:-0}"
PIN_USE_SMT="${PIN_USE_SMT:-0}"   # ccx mode: also put the SMT siblings in the mask

# Keep every job single-threaded regardless of what a library decides on its own
# (our binaries already pin Gurobi to Threads=1; external solvers may not).
# Under a taskset mask, a library that sizes its pool from the affinity mask sees
# only the slot's cores, so this is belt-and-braces.
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 \
       NUMEXPR_NUM_THREADS=1 OMP_PROC_BIND=false

# Per-job memory cap for the TARGET machine. EMPTY = no cap (default).
# Example: MEM_LIMIT=200G  -> any single job exceeding 200 GiB is killed and
# recorded as an OOM result line. The enforcement mechanism (systemd-run cgroup
# cap, or prlimit) is auto-detected at runtime by lib.sh:init_mem.
MEM_LIMIT="${MEM_LIMIT:-32G}"

# ---- Binaries -------------------------------------------------------------
BUILD="$REPO_DIR/build"
RUN_REDUCE="$BUILD/run_reduce"
RUN_ILP="$BUILD/run_ilp"
HG2G="$BUILD/hypergraph_to_graph"
GRC="$BUILD/graph_reduction_comparison"
CLIQUE_BLOWUP="$BUILD/clique_blowup"

# External comparison solvers.
STRUCTION="$REPO_DIR/../KaMIS/deploy/struction"
VC="$REPO_DIR/../WeGotYouCovered/optimized/vc_solver"
SATREDUCE="$REPO_DIR/../vc-satreduce/build/vc-bnb"
BM="$REPO_DIR/../Bmatching/build/app/bmatching_cli"
TRANSPOSE="$EXP_DIR/transpose_hgr.py"

export GUROBI_HOME="${GUROBI_HOME:-/home/ernestineg/gurobi/gurobi1203/linux64}"

# Scratch base for per-job temp files (kept off root /tmp; -j jobs materialize
# large clique-expansion graphs). Override TMPDIR to relocate.
SCRATCH_BASE="${TMPDIR:-$HOME/scratch}"

# Export the scalars the worker functions (run under GNU parallel) need. Arrays
# such as SEEDS stay in the main shell (used only by emit_pending via nameref).
export RES FAILURES HG_FULL HG_SOLVABLE T K MEM_LIMIT
export RUN_REDUCE RUN_ILP HG2G GRC CLIQUE_BLOWUP STRUCTION VC SATREDUCE BM TRANSPOSE
export SCRATCH_BASE

# Build the pinning slot table; this sets and exports the effective N_JOBS and
# PIN_SETS_STR (the per-slot `taskset -c` masks lib.sh applies to every job).
source "$EXP_DIR/topology.sh"
topo_init
export PIN_MODE
