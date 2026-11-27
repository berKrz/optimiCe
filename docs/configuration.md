# Configuration

Everything optimiCe runs is described in one TOML file, passed with `-f`. The file describes a *batch*: one or more runs, each with its own algorithm and parameters, all repeated over the same number of seeds.

```toml
[common]
n_dims     = 2
fitness    = "sphere"
domain_min = -5.0
domain_max = 5.0
iterations = 100
seed       = 42

[[run]]
algorithm  = "pso"
swarm_size = 30
w          = 0.7
```

## File structure

A file has three kinds of section:

- `[common]` (optional): settings shared by every run, such as the fitness function, the domain and the iteration budget, plus the batch-wide `seed` and `replicates`.
- `[defaults.<algo>]` (optional, one per algorithm): defaults for every run of that algorithm, e.g. `[defaults.pso]`.
- `[[run]]` (one or more): a run. `algorithm` selects its algorithm; any other key sets one of the algorithm's fields, or overrides a setting from `[common]`.

Fields come in two kinds: *runner fields*, which every run has whatever its algorithm (listed [below](#runner-fields)), and *algorithm fields*, which belong to one algorithm (listed in each [algorithm's doc](#algorithm-fields)).

## Syntax

- `#` starts a comment, on its own line or after a value. Blank lines are ignored.
- Only `[common]`, `[defaults.<algo>]` and `[[run]]` are allowed at the top level.
- `algorithm` is required in every `[[run]]`. It's read before anything else in the section, so its position inside the section doesn't matter.
- `seed` and `replicates` go in `[common]` only. The other runner fields can go in `[common]`, in `[[run]]`, or both.
- Algorithm fields go in `[[run]]` or in `[defaults.<algo>]` for their algorithm, never in `[common]`.
- A `[defaults.<algo>]` section must name a registered algorithm, but its keys are only checked when some `[[run]]` uses that algorithm. A block for an algorithm no run uses is skipped, so it can stay in the file while those runs are commented out.
- Keys are written exactly as listed in the field tables, with underscores, and each key may appear only once per section.
- Values are typed: `int` fields take integers (`30`, not `30.0`), `float` fields take floats or integers (`5` is read as `5.0`), and `enum` and `name` fields take quoted strings (`"sphere"`). `float[]` fields take arrays of numbers (`[0.3, 0.7]`).
- `inf` and `-inf` are valid floats wherever a field accepts them, such as an unbounded `domain_max`.
- `nan` is rejected, except in the optional fields, whose default is shown as (unset): there it means unset, so a `[[run]]` can use it to turn off a value set in `[common]` or `[defaults.<algo>]`.

## Batches

A batch is every `[[run]]` repeated `replicates` times; each repetition of a run is a *job*.

- **Labels.** Each run is labelled by its algorithm and its position among the runs of that algorithm: `pso-1`, `pso-2`, `ga-1`, … Labels follow the order of the `[[run]]` sections, so commenting a run out renumbers the ones after it.
- **Precedence.** A runner field takes its value from `[[run]]`, else from `[common]`, else its default. An algorithm field takes its value from `[[run]]`, else from `[defaults.<algo>]`, else its default.
- **Order.** Jobs are numbered run by run: every replicate of the first run, then every replicate of the second, and so on. Results are printed and written in that order.

```toml
[common]
fitness    = "rastrigin"
n_dims     = 10
iterations = 1000
replicates = 30
seed       = 42

[defaults.pso]
w     = 0.9
w_end = 0.4

[[run]]            # pso-1: decaying inertia, from [defaults.pso]
algorithm = "pso"

[[run]]            # pso-2: constant inertia
algorithm = "pso"
w_end     = 0.9

[[run]]            # ga-1
algorithm = "ga"
pop_size  = 40
```

## Seeds and reproducibility

`seed` is the batch's base seed: replicate `r` of every run uses `seed + r`, wrapping around at 2³². When `seed` is omitted, one is generated and printed with the results, so any batch can be rerun.

- **Common random numbers.** Replicate 3 of `pso-1` and replicate 3 of `ga-1` use the same seed, so every run faces the same sequence of seeds. Statistical tests that pair or block on replicates, like the Friedman test, match this design; tests that assume independent samples don't.
- **Reproducing a job.** A job's result depends only on its seed and its configuration. Setting `seed` to a job's seed, `replicates = 1`, and keeping only its run reproduces it exactly. Likewise, replicates 5 to 9 of a batch are `seed + 5` with `replicates = 5`.
- **Parallelism doesn't matter.** Jobs run in parallel, but no job depends on which thread ran it or when, so the results are the same on any machine and with any number of cores.

## Domain and initialization range

The *domain*, `domain_min` to `domain_max`, is the search space: every dimension shares the same bounds, and solutions are kept inside them. Either end can be infinite (`-inf`, `inf`), for fitness functions defined everywhere. The fitness function may restrict the domain and the number of dimensions further; see [Fitness functions](fitness.md).

The *init range*, `init_min` to `init_max`, is where the initial population is drawn from. It defaults to the domain, and can be narrowed to start the search away from the region where the optimum is known to be. It must lie inside the domain and be finite, so it's required at any end where the domain is infinite.

Algorithms differ in what they need from the domain and the init range: an algorithm may need a finite domain, or fall back on the init range where the domain is infinite. Each algorithm's doc says how it uses both.

## Boundary handling

Algorithms that move solutions continuously can push a coordinate outside the domain. Those with a `bounds` field bring it back with one of three modes:

- `clamp`: the coordinate is pinned to the bound it crossed.
- `reflect`: the coordinate is mirrored back inside, as if bouncing between the bounds, as many times as needed when it overshoots by more than the domain's width. When only one end of the domain is finite, it bounces off that end once.
- `random`: the coordinate is redrawn uniformly in the domain. This needs a domain that is finite, or infinite at both ends: there's no uniform draw from a domain with exactly one infinite end.

A coordinate can only cross a finite bound, so with both ends infinite the mode has nothing to do. A coordinate that overflows to infinity, in any mode, is pinned to the nearest finite limit: the domain's bound, or the init range's end on a side where the domain is infinite.

## Stopping

A run has two caps, `iterations` and `evaluations`, counting iterations (generations, for GA) and fitness evaluations. `budget` picks which of them is the run's **budget**: the cap that also paces the algorithm's schedules, such as PSO's inertia and SMA's move ranges. The budget must be set, to a positive value. The other cap is optional, `0` turning it off, and only stops a run early, as do:

- `target_fitness`: stops once the algorithm's current best is at least as good as the target.
- `stagnation`: stops after that many consecutive iterations without improving the best value seen so far. `0`, the default, disables it.

```toml
budget = "evaluations" # schedules follow the evaluations used
evaluations = 30000    # the budget
iterations = 2000      # an early stop on top of it
```

Every rule is checked at the end of each iteration, in the order target, stagnation, iterations, evaluations; when several are met in the same iteration, the first one is the reason reported. A run reports how many iterations it completed, how many evaluations it used, and why it stopped.

### Schedules

Under an iteration budget, a schedule moves one step per iteration and reaches its end on the budget's last iteration. Under an evaluation budget, it follows the share of the budget used so far, setup's evaluations included: an iteration doesn't know in advance how many evaluations it will use, so the schedule lands close to its end on the last iteration, by one iteration's share of the budget, rather than exactly on it. Either way, a run that stops early leaves its schedule unfinished.

So the budget is part of what an experiment is: two runs with the same seed, one under `budget = "iterations"` and one under `budget = "evaluations"`, follow different schedules from the first iteration, even if they stop at the same point.

### Evaluation cutoff

An iteration evaluates many candidates, and the evaluation cap usually runs out partway through one. `evaluation_cutoff` says what happens to the rest of it, whenever `evaluations` is set, budget or not:

- `exact` (the default): evaluations past the cap aren't made. Each gets the worst fitness there is, so it can't become the best, and isn't counted: the run uses exactly `evaluations` evaluations, as fixed-budget benchmarks require. The iteration still finishes, and the result is the best of the evaluations actually made.
- `iteration`: the iteration finishes with every evaluation made, so a run can use up to one iteration's worth more than the cap.

A cap smaller than the algorithm's setup isn't an error: setup runs, then one iteration, and the run stops. Under `exact`, some of the initial population is then never evaluated, and an algorithm that reports its final population, such as GA without elitism, can report a result that wasn't evaluated either.

## Validation

Every check runs at startup, before the first job: a configuration either runs completely or not at all. Unknown keys, wrongly typed values and keys in the wrong section are errors, and so are values out of range, combinations that don't work together, and limits of the fitness function. Range and cross-field checks run on each run's merged configuration, after the whole file is read; a problem that comes from `[common]` is reported against the first run it affects.

Messages name the file, the section and the key, and a run by its label:

```
Error at 'example.toml [[run]] pso-1' [swarm-size]: unknown key for algorithm 'pso'
Error at 'example.toml [[run]] pso-1': for eggholder, n_dims must be at most 2 (current: 3)
Error at 'example.toml [[run]] sma-1': budget is "evaluations", so evaluations must be positive
Error at 'example.toml [[run]] ga-1' [seed]: only allowed in [common]
```

A run whose `algorithm` is missing or unknown has no label yet, and is named by its position instead: `[[run]] #3`.

## Runner fields

The fields every run has, whatever its algorithm.

### In `[common]` only

These belong to the whole batch, so no run can override them.

<!-- gen:fields runner common -->
<!-- Generated by `make docs` from the source tables; edits here are overwritten -->

| Key | Type | Default | Values | Description |
|-----|------|---------|--------|-------------|
| `seed` | uint32 | (generated) |  | Base seed; replicate r of every run uses seed + r |
| `replicates` | int | `1` |  | Times every run is repeated, each with its own seed |

<!-- /gen -->

### In `[common]` or `[[run]]`

A value in `[[run]]` overrides the one in `[common]`.

<!-- gen:fields runner common,run -->
<!-- Generated by `make docs` from the source tables; edits here are overwritten -->

| Key | Type | Default | Values | Description |
|-----|------|---------|--------|-------------|
| `direction` | enum | `minimize` | `minimize`, `maximize` | Optimization direction |
| `n_dims` | int | `2` |  | Number of dimensions of the search space |
| `budget` | enum | `iterations` | `iterations`, `evaluations` | Which cap paces schedules; the other, when set, only stops a run early |
| `iterations` | int | `0` |  | Iteration cap (generations, for GA); 0 = off, unless it's the budget |
| `evaluations` | int | `0` |  | Fitness evaluation cap; 0 = off, unless it's the budget |
| `evaluation_cutoff` | enum | `exact` | `exact`, `iteration` | How the evaluation cap ends its last iteration: skip the evaluations past it, or finish the iteration |
| `target_fitness` | float | (unset) |  | Stop early once the best is at least this good |
| `stagnation` | int | `0` |  | Stop early after this many iterations without improving (0 = off) |
| `domain_min` | float | `-5.0` |  | Lower bound of every dimension (-inf = unbounded below) |
| `domain_max` | float | `5.0` |  | Upper bound of every dimension (inf = unbounded above) |
| `init_min` | float | (unset) |  | Lower end of the initial population's range (unset = domain_min) |
| `init_max` | float | (unset) |  | Upper end of the initial population's range (unset = domain_max) |
| `fitness` | name | `sphere` | `sphere`, `rosenbrock`, `rastrigin`, `griewank`, `ackley`, `eggholder`, `schwefel`, `schaffer_f6` | Benchmark function to optimize (see --list) |

<!-- /gen -->

### In `[[run]]` only

<!-- gen:fields runner run -->
<!-- Generated by `make docs` from the source tables; edits here are overwritten -->

| Key | Type | Default | Values | Description |
|-----|------|---------|--------|-------------|
| `algorithm` | name | (required) | `ga`, `pso`, `sma` | Algorithm to run (see --list) |

<!-- /gen -->

## Algorithm fields

Each algorithm's fields, with what they do and how they interact, are in its own doc:

<!-- gen:algorithms -->
<!-- Generated by `make docs` from the source tables; edits here are overwritten -->

- [GA - Genetic Algorithm](algorithms/ga.md) (`ga`)
- [PSO - Particle Swarm Optimization](algorithms/pso.md) (`pso`)
- [SMA - Slime Mould Algorithm](algorithms/sma.md) (`sma`)

<!-- /gen -->
