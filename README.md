# optimiCe

A runner for metaheuristic optimization algorithms, written in C.

optimiCe runs optimization algorithms against a chosen benchmark fitness function and reports the best solution each run finds. One configuration file describes a whole batch: any number of runs, each with its own algorithm and parameters, repeated over a set number of seeds. The algorithms, fitness function, and all parameters are specified in a TOML configuration file.

## Common

- Eight standard benchmark fitness functions: `sphere`, `rosenbrock`, `rastrigin`, `griewank`, `ackley`, `eggholder`, `schwefel`, `schaffer_f6`
- N-dimensional continuous search space, bounded or not: either end of the domain can be infinite where the fitness function is defined there (`sphere` and `rosenbrock`), for PSO and SMA; the GA's encoding needs a finite domain
- Initialization range separate from the domain (`init_min`, `init_max`), for starting the search in a region narrower than its bounds, or anywhere at all when it has none
- Minimization or maximization
- Iteration budget shared by every algorithm (`iterations`; generations, for GA), with two optional early stoppers: `target_fitness` ends the run once the current best is at least as good as the target, and `stagnation` ends it after that many consecutive iterations without improving the best-ever. The final result reports how many iterations ran, and says when and why a run stopped early
- Every run counts its fitness evaluations, the usual cost measure for comparing metaheuristics: iterations cost different amounts per algorithm and population size, evaluations don't. Each candidate solution is evaluated exactly once when it's created, so the count is the run's real cost
- Batches: several `[[run]]` sections in one file, each repeated `replicates` times, with shared settings in `[common]` and per-algorithm defaults in `[defaults.<algo>]` (see [Batches](#batches))
- Parallel execution: a batch's jobs run on as many threads as there are CPU cores, with results and output identical to a sequential run
- Reproducible runs: every job prints the seed it ran with, and that seed reruns it exactly. `seed` can be set in the configuration; when omitted, one is generated and printed
- Configuration errors caught at startup, before anything runs, with messages pointing at the offending section, run, and key
- Interactive step-through mode (`-i`) for inspecting a single run
- `--dump-config` prints every run's resolved configuration for debugging

## PSO

- Classical velocity update: `v ← w·v + c₁·r₁·(pbest − x) + c₂·r₂·(gbest − x)`
- Optional linear inertia decay: `w` applies at the first iteration and `w_end` at the last, interpolated in between (Shi & Eberhart's `0.9 → 0.4` is the classic setting). Leaving `w_end` unset keeps the inertia constant
- Optional velocity clamping (`v_max`): each velocity component is capped at `v_max × (domain_max − domain_min)`; when the domain is unbounded, the init range's width is used instead
- Asynchronous or synchronous gbest update (`gbest_update`): `async` moves gbest within the iteration as soon as any particle improves it, `sync` refreshes it once after the whole swarm has moved
- Configurable boundary handling (`bounds`): `clamp` pins a particle to the bound and zeroes its velocity on that dimension (the default), `reflect` mirrors it back inside and reverses that velocity component, `random` redraws that coordinate uniformly in the domain and zeroes its velocity
- Configurable inertia (`w`), cognitive coefficient (`c1`), and social coefficient (`c2`)
- `pbest` initialization by particle position, or by independent random draw from the init range

## GA

- Binary genetic algorithm over an N-dimensional continuous search space: each individual carries `n_dims × ind_size` bits, with each dimension's `ind_size`-bit slice linearly decoded onto `[domain_min, domain_max]`
- Initial population of random bits, or, under an init range narrower than the domain, of the bit patterns that decode inside it
- Four selection operators:
  - `roulette`: fitness-proportional, with weights shifted so the worst individual gets weight 1 (`max − f + 1` when minimizing, `f − min + 1` when maximizing). Works with negative fitness, but the `+1` is in fitness units, so selection pressure depends on the scale of the fitness values
  - `roulette_normalized`: like `roulette`, but the shift past the worst individual is the population's average fitness spacing (`range / (pop_size − 1)`) instead of 1 fitness unit. Pressure doesn't depend on the fitness scale, and the worst individual keeps a small chance of being picked
  - `rank`: linear ranking. Individuals are weighted by their rank alone; `rank_pressure` sets the expected number of copies of the best individual (the worst gets `2 − rank_pressure`)
  - `tournament`: best of `tournament_size` individuals drawn at random
- Two crossover operators:
  - `n_point`: dimension-aware n-point crossover. `cut_points` lists the cut ratios, and they apply independently within each dimension's slice. Every cut must actually split the slice: ratios landing on bit 0, or two ratios landing on the same bit, are rejected because they'd silently yield fewer cuts than listed. Segments between cuts alternate keep / swap, starting with keep, so `[0.6]` is classic single-point and `[0.3, 0.7]` is two-point
  - `uniform`: every bit is exchanged independently with probability 0.5
- Optional family-competition elitism (`elitism = "family"`): after crossover and mutation, each pair keeps the best two of its two parents and two children (children win ties). An unpaired last individual keeps the better of itself and its mutant. Costs no extra fitness evaluations: every child is evaluated once either way, and the parents' fitness is already known
- Per-bit mutation at a configurable rate
- `ind_size` capped at 63 so the per-dimension decode fits in a single `uint64_t`

## SMA

- Rank-based weighting: at each iteration slimes are ranked by fitness; the better half gets weights `1 + r·log₁₀((bF − fᵢ)/S + 1)`, the worse half gets `1 − r·log₁₀(…)` — better rank attracts, worse rank repels
- Three-branch position update per slime: with probability `z`, full uniform re-init into the init range; otherwise, per-dimension, probability `tanh|fᵢ − DF|` of exploiting around the best-ever using two random peers and amplitude `a = atanh(1 − t/T)`; else contract with amplitude `b = 1 − t/T`
- Decay: both `a` and `b` shrink to zero as `t → T`, trading exploration for exploitation across the run
- Opportunistic best-ever update within the iteration (mirrors PSO's async gbest — later slimes see the fresh best)
- Configurable boundary handling (`bounds`): `clamp` pins a coordinate to the bound (the default), `reflect` mirrors it back inside, `random` redraws it uniformly in the domain

## Build

optimiCe depends on [tomlc17](https://github.com/cktan/tomlc17) for TOML parsing. With it available:

```bash
make
```

produces the `optimice` binary in the repo root. The Makefile locates tomlc17 through `pkg-config` (`libtomlc17.pc`), and falls back to a plain `-ltomlc17` if `pkg-config` can't find it.

**With Nix**, the dev shell already provides tomlc17 (see [Dev environment](#dev-environment)). The flake also builds the binary directly:

```bash
nix build    # binary at ./result/bin/optimice
```

**Without Nix**, build and install tomlc17 from upstream first:

```bash
git clone https://github.com/cktan/tomlc17
cd tomlc17
make
sudo make install    # installs to /usr/local by default
```

If you install it under a non-default prefix, point `pkg-config` at it when building optimiCe, e.g. `PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig make`.

## Usage

```bash
./optimice -f example.toml
```

| Flag        | Long form         | Description                                                              |
|-------------|-------------------|--------------------------------------------------------------------------|
| `-f FILE`   | `--config FILE`   | Path to TOML configuration file (required)                               |
| `-i`        | `--interactive`   | Pause after each iteration; the batch must be a single job               |
| `-l`        | `--list`          | List available algorithms                                                |
| `-d`        | `--dump-config`   | Print the resolved configuration and exit                                |
| `-o [PATH]` | `--output [PATH]` | Also write the results to a CSV file (see [Results file](#results-file)) |
| `-h [ALG]`  | `--help [ALG]`    | General help, or algorithm-specific help if `ALG` is given               |

Short options work in either case: `-F` is `-f`.

### Results file

`-o` writes the results to a file as well as printing them; the terminal output doesn't change. Where the file goes depends on the path:

- `-o` with no path: a generated name in the current directory, `<config name>-<YYYYMMDD-HHMMSS>.csv` (e.g. `example-20261109-143205.csv`)
- `-o results/`, ending in a slash, or naming a directory that already exists: a generated name inside it
- `-o results/run.csv`: that file, replaced if it already exists

Missing directories are created. A generated name never replaces an existing file: a second run of the same config within the same second gets `-2`, `-3`, … appended. The chosen path is printed to stderr (`Output: …`). `--dump-config` exits before the file is opened, so it never creates or touches one.

The file starts with `#` comment lines saying where the results came from:

```
# optimice 85d62f0-dirty
# config: experiments/rastrigin.toml (cksum 114835441 2397)
# date: 2026-11-09 14:32:05 -0300
# jobs: 12
```

The first line is the commit the binary was built from (`-dirty` if the tree had uncommitted changes). The second is the config file as given on the command line, with its `cksum`: running `cksum experiments/rastrigin.toml` prints the same two numbers as long as the file hasn't changed since. The date is local time, with its UTC offset.

After the header comes a plain CSV table, one row per job, in the same order as the terminal lines:

```
run,replicate,seed,fitness,iterations,evaluations,stop,position
pso-1,0,0x0000002a,0.000916229158228532,421,12660,target,0.001419190827760949 -0.001131818227894787 ...
pso-1,1,0x0000002b,0.4381614526478135,500,15030,none,-0.031086559132914433 -0.020465036735787943 ...
```

| Column        | Meaning                                                                          |
|---------------|----------------------------------------------------------------------------------|
| `run`         | The run's label, as in the terminal output                                       |
| `replicate`   | Replicate number, from 0                                                         |
| `seed`        | The job's seed, in hex; it can be pasted back into `seed =` to reproduce the job |
| `fitness`     | Best fitness found                                                               |
| `iterations`  | Iterations completed (fewer than the budget after an early stop)                 |
| `evaluations` | Fitness evaluations used                                                         |
| `stop`        | `none`, `target` or `stagnation`                                                 |
| `position`    | The best position, its coordinates separated by spaces                           |

Numbers are written in the shortest form that reads back as the exact value, so the file holds the same doubles the run produced. Most CSV readers skip the header given a comment character, e.g. `pandas.read_csv(path, comment="#")` or `read.csv(path, comment.char = "#")` in R. The file is plain CSV with `.` as the decimal separator and `,` between fields.

Rows are written as jobs finish, in job order, and each one is flushed immediately: a batch that's interrupted leaves every row it completed. Comparing the number of rows with `# jobs:` tells a complete file from an interrupted one. The file is the same whatever the number of threads, apart from the date.

## Configuration

All parameters are set in a TOML file passed via `-f`, split into a `[common]` section for algorithm-agnostic fields, optional `[defaults.<algo>]` sections with defaults for one algorithm, and one or more `[[run]]` sections, each carrying an algorithm selection and its algorithm-specific fields. See [`example.toml`](example.toml) for a fuller example covering every algorithm.

```toml
# optimice example config - minimal PSO run
[common]
n_dims     = 2
fitness    = "sphere"
direction  = "minimize"
domain_min = -5.0
domain_max =  5.0
iterations = 100
seed       = 42

# The run: algorithm selection + its fields
[[run]]
algorithm  = "pso"
swarm_size = 30
w          = 1.0
c1         = 2.0
c2         = 2.0
pbest_init = "position"
```

### Syntax

- `#` starts a comment, either on its own line or after a value. Blank lines are ignored.
- Only three kinds of top-level sections are allowed: `[common]` (optional), `[defaults.<algo>]` (optional, one per algorithm) and `[[run]]` (required, one or more).
- `algorithm` is required in every `[[run]]`. It selects the algorithm and loads its defaults before anything else is applied, so its position inside the section doesn't matter.
- `seed` and `replicates` go in `[common]` only.
- The other runner fields can go in `[common]`, in `[[run]]`, or both. A value in `[[run]]` overrides the one in `[common]`.
- Algorithm fields go in `[[run]]` or in `[defaults.<algo>]` for their algorithm. A value in `[[run]]` overrides the one in `[defaults.<algo>]`.
- A `[defaults.<algo>]` section must name a registered algorithm, but its keys are only checked when some `[[run]]` uses that algorithm: a block for an algorithm no run uses is skipped, so it can stay in the file while those runs are commented out.
- Keys are written exactly as listed in the tables below, with underscores.
- Each key may appear only once per section. TOML rejects duplicates at parse time.
- Values are typed: `int` fields take integers (`30`, not `30.0`), `float` fields take floats or integers (`5` is read as `5.0`), and `enum` / `name` fields take quoted strings (`"sphere"`).
- `nan` is rejected, except in the optional float fields (`target_fitness`, `init_min`, `init_max`, `w_end`), where it means unset: a `[[run]]` can use it to turn off a value set in `[common]` or `[defaults.<algo>]`.
- Unknown keys, wrongly typed values, and keys in the wrong section are hard errors at startup. Messages name the file, section, and key, and a run by its label, e.g. `Error at 'example.toml [[run]] pso-2' [swarm-size]: unknown key for algorithm 'pso'`. A run whose `algorithm` is missing or unknown has no label yet and is named by its position instead (`[[run]] #3`).
- Range and cross-field checks run on each run's merged configuration, after the whole file is read: `domain_min < domain_max`, each fitness function's `n_dims` and domain requirements, and each algorithm's own constraints. A problem that comes from `[common]` is reported against the first run it affects.
- Every check runs before the first job starts, so a configuration either runs completely or not at all.

### Batches

A batch is every `[[run]]` repeated `replicates` times; each repetition of a run is a job.

- **Labels.** Each run is labelled by its algorithm and its position among the runs of that algorithm: `pso-1`, `pso-2`, `ga-1`, … The label depends on the order of the `[[run]]` sections, so commenting a run out renumbers the ones after it.
- **Precedence.** A runner field takes its value from `[[run]]`, else `[common]`, else its default. An algorithm field takes it from `[[run]]`, else `[defaults.<algo>]`, else its default.
- **Seeds.** `seed` is the batch's base seed: replicate `r` runs with `seed + r` (wrapping around at 2³²), and that's true in every run. Replicate 3 of `pso-1` and replicate 3 of `ga-1` use the same seed, so every run faces the same sequence of seeds (common random numbers). Statistical tests that pair or block on replicates, like Friedman, match this design; tests that assume independent samples don't.
- **Reproducing a job.** Every job prints its seed. Setting `seed` to it, `replicates = 1`, and keeping only that run reproduces the job exactly. Likewise, replicates 5 to 9 of a batch are `seed + 5` with `replicates = 5`.
- **Parallelism.** Jobs run on `min(jobs, CPU cores)` threads, each taking the next job as soon as it finishes one; there's no flag to set it. A job's result depends only on its seed, never on which thread ran it or when, so the output is identical whatever the core count.
- **Output.** A single job prints a line per iteration and a full final result. A batch with more than one job prints one line per job instead, with its label, replicate, seed, best fitness, iterations completed, evaluation count, and early stop if any. Lines come out in job order (every replicate of the first run, then the second, …), each as soon as it and all jobs before it have finished. The number of threads goes to stderr, so redirected output stays the same on every machine. Final results (the single-job fitness and each job line's best) are printed in the shortest form that reads back as the exact value, per-iteration lines keep 6 significant digits.
- **Failures.** Every configuration check runs before the first job starts. A failure while jobs run (such as running out of memory) stops the whole batch with an error naming the job, e.g. `Error at 'pso-2, replicate 7': memory allocation failed`; lines already printed stay printed.
- **`--interactive`** needs a single job: one `[[run]]` and `replicates = 1`.
- **`--dump-config`** prints the base seed and `replicates` once, then each run's full resolved configuration, defaults included.

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

### Runner fields

Fields that belong to the runner rather than to any algorithm.

| Key              | Type   | Default    | Description                                                                                                  |
|------------------|--------|------------|--------------------------------------------------------------------------------------------------------------|
| `seed`           | uint32 | generated  | Base seed; replicate `r` of every run uses `seed + r`. When omitted, one is generated and printed. `[common]` only |
| `replicates`     | int    | `1`        | Number of times every run is repeated, each with its own seed. At least 1. `[common]` only                   |
| `direction`      | enum   | `minimize` | `minimize` or `maximize`                                                                                     |
| `n_dims`         | int    | `2`        | Number of dimensions, at least 1                                                                             |
| `iterations`     | int    | `100`      | Iteration budget, the hard cap on every run (generations, for GA)                                            |
| `target_fitness` | float  | unset      | Stop early once the current best is at least as good as this value                                           |
| `stagnation`     | int    | `0`        | Stop early after this many consecutive iterations without improving the best-ever; `0` disables it           |
| `domain_min`     | float  | `-5.0`     | Lower bound of each dimension; `-inf` leaves the search unbounded below                                      |
| `domain_max`     | float  | `5.0`      | Upper bound of each dimension; `inf` leaves the search unbounded above                                       |
| `init_min`       | float  | (unset)    | Lower end of the range the initial population is drawn from; unset means `domain_min`. Must be finite and inside the domain; required when `domain_min` is `-inf` |
| `init_max`       | float  | (unset)    | Upper end of the range the initial population is drawn from; unset means `domain_max`. Must be finite and inside the domain; required when `domain_max` is `inf` |
| `fitness`        | name   | `sphere`   | One of the eight benchmark functions listed above. Some have fixed `n_dims` or stricter domain bounds, enforced at startup |
| `algorithm`      | name   | required   | `pso`, `ga` or `sma`. `[[run]]` only                                                                         |

### PSO fields

| Key            | Type  | Default         | Description                                                                              |
|----------------|-------|-----------------|------------------------------------------------------------------------------------------|
| `swarm_size`   | int   | `30`            | Number of particles                                                                      |
| `w`            | float | `1.0`           | Inertia weight (at the first iteration, when `w_end` is set)                             |
| `w_end`        | float | unset (= `w`)   | Inertia at the last iteration; linear decay from `w`. Unset means constant inertia       |
| `v_max`        | float | `0.0`           | Velocity cap as a fraction of the domain width, or of the init range's width when the domain is unbounded; `0` disables it |
| `c1`           | float | `2.0`           | Cognitive coefficient                                                                    |
| `c2`           | float | `2.0`           | Social coefficient                                                                       |
| `pbest_init`   | enum  | `position`      | `position` (pbest starts at the initial position) or `random` (independent random point) |
| `gbest_update` | enum  | `async`         | `async` (gbest moves as soon as a particle improves it) or `sync` (once per iteration)   |
| `bounds`       | enum  | `clamp`         | Out-of-domain handling: `clamp`, `reflect` or `random`. Reflection bounces as many times as needed for coordinates that overshoot by more than the domain width. `random` isn't available on a domain with exactly one infinite end, and the field is ignored when both ends are infinite |

### GA fields

| Key               | Type    | Default    | Description                                                                                 |
|-------------------|---------|------------|---------------------------------------------------------------------------------------------|
| `pop_size`        | int     | `15`       | Population size (minimum 2)                                                                 |
| `ind_size`        | int     | `22`       | Bits per dimension, in `[2, 63]`. Total bits per individual is `n_dims × ind_size`          |
| `tournament_size` | int     | `2`        | Individuals per tournament; must be at most `pop_size`. Only used when `selection=tournament` |
| `rank_pressure`   | float   | `1.5`      | Expected copies of the best individual under rank selection, in `[1.0, 2.0]`: `1.0` is uniform, `2.0` never picks the worst. Only used when `selection=rank` |
| `cut_points`      | float[] | `[0.6]`    | Cut ratios for `n_point`, each in `(0.0, 1.0)` and strictly increasing; each cut index is `floor(ind_size × ratio)` per dimension, and indices must be distinct and at least 1. Only used when `crossover=n_point` |
| `mutation_rate`   | float   | `0.01`     | Per-bit flip probability in `[0.0, 1.0)`                                                    |
| `selection`       | enum    | `roulette` | `roulette`, `roulette_normalized`, `rank` or `tournament`                                   |
| `crossover`       | enum    | `n_point`  | `n_point` or `uniform`                                                                      |
| `elitism`         | enum    | `none`     | `none` or `family`                                                                          |

### SMA fields

| Key        | Type  | Default | Description                                                    |
|------------|-------|---------|----------------------------------------------------------------|
| `pop_size` | int   | `30`    | Population size                                                |
| `z`        | float | `0.03`  | Full re-init probability per slime, in `[0.0, 1.0]`; a re-initialized slime is drawn from the init range |
| `bounds`   | enum  | `clamp` | Out-of-domain handling: `clamp`, `reflect` or `random` (not on a domain with exactly one infinite end). Ignored when both ends are infinite |

## Dev environment

The repo includes a Nix flake with the C toolchain, tomlc17, and the tools used during development:

```bash
nix develop
```

enters a shell with `gcc`, `pkg-config`, `tomlc17`, `clang-tools`, `gdb` and `bear`. With [direnv](https://direnv.net) installed, the shell activates automatically on `cd` into the directory: copy `.envrc.example` to `.envrc` and run `direnv allow`.
