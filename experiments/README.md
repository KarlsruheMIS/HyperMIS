# `experiments/` — resumable, failure-recording experiment pipeline

Every experiment behind the paper lives here, and every one can be **restarted at
any time**: a rerun only executes the `(instance, seed[, config])` runs not
already present in the output file, one instance failing never stops the batch,
and every OOM/crash is recorded with its reason in `results/FAILURES.tsv`.

Output goes to `results/*.tsv` (`RED/`, `ILP/`, `GRAPH/`), one file per
experiment block.

## Quick start

```bash
experiments/run_all.sh --status     # what's done / missing / failed (read-only)
experiments/run_all.sh              # status, then run every experiment (missing only)
experiments/run_all.sh --only bmatching
experiments/run_all.sh --force      # rerun everything from scratch (backs up old TSVs)
```

Or run one experiment directly (same resume behavior):

```bash
experiments/run_experiment.sh
experiments/run_reduction_statistics.sh
experiments/run_graph_reduction_comparison.sh
experiments/run_bmatching_experiments.sh
experiments/run_graph_solver_experiments.sh
```

## CPU pinning — trustworthy timings

Every parallel slot is bound to a fixed, disjoint set of physical cores inside
**one L3 domain** (a CCX on AMD). Unpinned, the kernel migrates a job across L3
domains mid-run and may co-schedule two jobs on the two SMT threads of one
physical core; both cost wall time at random, which is why the *same*
deterministic reduction times differently under different seeds.

The layout is read from `/sys` at runtime — nothing machine-specific is
hard-coded. Inspect it any time:

```bash
experiments/topology.sh                       # slot table for the configured mode
PIN_MODE=core N_JOBS=32 experiments/topology.sh
```

| `PIN_MODE` | slots | what each job gets |
|------------|-------|--------------------|
| `ccx` *(default)* | one per L3 domain (16 on the EPYC 7702P) | a whole CCX — private 16 MiB L3, never migrated out |
| `core` | `N_JOBS`, carved from the CCX-ordered physical cores | `N_JOBS=32` → 2 cores, 2 jobs/CCX (share L3, never a core); `N_JOBS=64` → 1 core each |
| `off` | `N_JOBS` | nothing — the old unpinned behaviour |

`N_JOBS=0`/`auto` (the default) means *one job per L3 domain*. Pick a divisor or
multiple of the domain count; anything else makes a slot straddle a CCX and is
reported as a warning at startup. `PIN_USE_SMT=1` puts the SMT siblings in the
mask too (off by default: idle siblings are what keep a job's core exclusive).

The driver — this shell, GNU parallel, and the `awk`/`date`/`mktemp` calls
`run_capture` makes around each job — is confined to the cpus no slot owns (the
idle SMT siblings), so the harness never preempts a job it is timing.
`PIN_HOUSEKEEPING=0` disables that.

Measured on `aeghpc110` (EPYC 7702P, 16 CCX × 4 cores), 24 instances × 3
repetitions of the identical deterministic reduction:

| mode | jobs | mean per-instance CV | worst instance | Σ mean job time |
|------|------|---------------------|----------------|-----------------|
| `off`  | 32 | 2.9 % | 11.9 % (1.30×) | 267.2 s |
| `core` | 32 | 1.6 % |  7.9 % (1.21×) | 267.0 s |
| `ccx`  | 16 | **0.8 %** |  6.6 % (1.15×) | 263.3 s |

Per-job times are essentially unchanged across modes, so `ccx` trades **batch
wall-clock** (half the concurrency) for clean numbers — not job speed. Use
`PIN_MODE=core N_JOBS=32` when you want the old width with core-exclusive slots.
Both are far more stable than unpinned; whichever you pick, keep it fixed for a
whole result set, since timings are only comparable within one mode.

## Configuration — `experiments/config.sh`

One place for everything: `SEEDS`, `N_JOBS`, `PIN_MODE`, `T`/`K` budgets,
instance-set paths, binary paths, and `MEM_LIMIT`. Nothing about any specific
machine is hard-coded.

### Instance sets

The defaults are the hypergraphs **bundled with the repo**, and both sets are the
**same folder**, so a fresh clone runs out of the box with nothing to generate:

| variable | default | used by |
|----------|---------|---------|
| `HG_FULL` | `hypergraphs/` | the reduction blocks |
| `HG_SOLVABLE` | `$HG_FULL` — the same folder | the ILP and exact-solver blocks |

Everything runs on every instance. That is also the only correct starting point:
which instances are solvable is not known until the solvers have been run on all
of them. A fresh clone is just:

```bash
mkdir build && cd build && cmake .. && make && cd ..
experiments/run_all.sh
```

**Narrowing the second set is optional and manual.** It is worth doing once you
have results and the expensive solver comparisons would otherwise spend hours on
instances nothing finishes. `collect_solvable.sh` builds a folder of symlinks to
every instance that **any** solver has proved optimal — the ILP variants, the
graph-ILP after the KaMIS reductions, struction / vc_solver / satreduce, and the
b-matching route, raw and reduced. One solver on one seed is enough: the point is
to exclude what nothing can finish, not to prefer a particular solver.

It does *not* change what the pipeline reads — it prints the one line that does:

```bash
experiments/collect_solvable.sh
export HG_SOLVABLE=/path/it/printed
```

Which tables count is discovered from their headers, so a new solver block is
picked up as soon as it writes results. The `opt` column is located **by name**:
the reduction tables are the same shape but mean something else (`gred.tsv` has
the graph vertex count where the solver tables have `opt`), and they are skipped
rather than misread.

All three of `HG_FULL`, `HG_SOLVABLE` and `RES` are env-overridable, so pointing
the pipeline at a larger collection — or at a scratch output dir for a trial run
— needs no edit:

```bash
HG_FULL=~/test_instances/hypergraphs \
HG_SOLVABLE=~/test_instances/hypergraphs_ilp_solvable \
  experiments/run_all.sh --status

RES=/tmp/trial experiments/run_experiment.sh     # write elsewhere, keep results/ intact
```

A missing or empty instance dir is reported at startup: otherwise every block
over it silently reports "0/0 groups, nothing to do" and looks like it ran.

### External solvers

`struction`, `vc_solver`, `vc-bnb` and `bmatching_cli` are separate projects; the
defaults assume they are checked out next to this repo, which a clone elsewhere
will not be. Missing ones are listed at startup — a solver that is absent would
otherwise surface as one "command not found" failure per (instance, seed),
looking like a crash rather than a missing install. Override the ones you have:

```bash
STRUCTION=/path/to/struction VC=/path/to/vc_solver \
  experiments/run_graph_solver_experiments.sh
```

Only their own comparison blocks depend on them; reductions and ILP do not.

- **`MEM_LIMIT`** (default empty = no cap). Set e.g. `MEM_LIMIT=200G` for the
  machine you run on. Each job that exceeds it is killed and its outcome recorded
  (see below). The enforcement mechanism is auto-detected at startup:
  `systemd-run --user --scope` (clean cgroup-v2 OOM signal) if available, else
  `prlimit --as`, else uncapped with a warning.

## How resume works

Each output file is keyed on **`(graph, seed)`** (`graph` = column 1, `seed` =
the `seed` column — both read straight from the file header). A group is
*complete* when it holds the expected number of rows:

- most blocks: **1** row per `(graph, seed)`;
- `stats.tsv`: **any** rows (the per-rule dump emits several rows per instance);
- `config_stats.tsv`: keyed one level finer, on **`(graph, seed, algo)`** — each
  of the reduction configs listed in `CONFIGS` (config.sh; `algo = reduce<c>`)
  is its own resumable
  unit and runs as its own job. Delete a subset of configs and *only those*
  rerun (via `run_block_sub` instead of `run_block`).

On start, `run_block` drops malformed rows and any partially-written group, then
runs only the still-incomplete groups. Rows are appended as jobs finish
(`parallel` is used **without `-k`**, so a crash never loses buffered lines, and
**without `--halt`**, so one failure never stops the batch). Row order does not
matter to any downstream consumer.

## Failure handling

Per job, `run_capture` classifies the outcome and reacts:

| outcome   | signal                              | what happens |
|-----------|-------------------------------------|--------------|
| SUCCESS   | exit 0                              | normal result row appended |
| TIMEOUT   | `timeout` exit 124                  | normal unsolved row (`opt=0`), as before — not a failure |
| OOM       | killed at `MEM_LIMIT` (exit 137/…)  | **exact-solver blocks:** an unsolved result row (`size=0 opt=0`) is written *and* an `oom` line is logged, so it counts as done and is **not** retried. **reduction blocks:** logged only (no natural null row) and retried next run |
| ERROR     | any other non-zero / empty stdout   | **no** result row, an `error` line with the stderr snippet is logged, and it is **retried** on the next run |

A non-zero exit always overrides any stdout the binary printed (covers
`graph_reduction_comparison -i`, which prints its line before verifying it), so a
wrong/unverified solution never lands in the results.

`results/FAILURES.tsv` columns: `timestamp  experiment  graph  algo  seed
exit_code  reason  stderr`.

## Files

| file | purpose |
|------|---------|
| `config.sh` | all settings |
| `topology.sh` | CPU pinning: L3-domain detection, slot table (`experiments/topology.sh` to print it) |
| `lib.sh` | engine: resume bookkeeping, `run_capture`, failure logging, `run_block` |
| `run_experiment.sh` | reductions (nred/fred/red) + graph-ILP (gfilp/gfrilp) |
| `run_reduction_statistics.sh` | per-rule `stats.tsv` + per-config `config_stats.tsv` |
| `run_graph_reduction_comparison.sh` | `gred.tsv` + `grilp.tsv` |
| `run_bmatching_experiments.sh` | `bmatching.tsv` + `rbmatching.tsv` |
| `run_graph_solver_experiments.sh` | struction / vc_solver / satreduce (× raw/reduce) |
| `status.sh` | read-only progress table |
| `run_all.sh` | orchestrator (`--status`, `--only`, `--force`, `--yes`) |
| `collect_solvable.sh` | optional: symlink folder of every instance ANY solver proved optimal, to point `HG_SOLVABLE` at |
| `transpose_hgr.py` | hypergraph transpose, used by the b-matching duality |
