# optimiCe

A runner for metaheuristic optimization algorithms, written in C.

optimiCe runs an optimization algorithm against a chosen benchmark fitness function and reports the best solution it finds. The algorithm, fitness function, and all parameters are specified in a TOML configuration file.

## Common

- Eight standard benchmark fitness functions: `sphere`, `rosenbrock`, `rastrigin`, `griewank`, `ackley`, `eggholder`, `schwefel`, `schaffer_f6`
- N-dimensional continuous search space
- Minimization or maximization
- Iteration budget shared by every algorithm (`iterations`; generations, for GA), with two optional early stoppers: `target_fitness` ends the run once the current best is at least as good as the target, and `stagnation` ends it after that many consecutive iterations without improving the best-ever. The final result says when and why a run stopped early
- Reproducible runs: `seed` can be set in the configuration; when omitted, one is generated and printed in the final result so any run can be rerun exactly
- Configuration errors caught at startup, with messages pointing at the offending section and key
- Interactive step-through mode (`-i`) for inspection
- `--dump-config` prints the resolved configuration for debugging

## PSO

- Classical velocity update: `v ← w·v + c₁·r₁·(pbest − x) + c₂·r₂·(gbest − x)`
- Optional linear inertia decay: `w` applies at the first iteration and `w_end` at the last, interpolated in between (Shi & Eberhart's `0.9 → 0.4` is the classic setting). Leaving `w_end` unset keeps the inertia constant
- Optional velocity clamping (`v_max`): each velocity component is capped at `v_max × (domain_max − domain_min)`
- Asynchronous or synchronous gbest update (`gbest_update`): `async` moves gbest within the iteration as soon as any particle improves it, `sync` refreshes it once after the whole swarm has moved
- Configurable boundary handling (`bounds`): `clamp` pins a particle to the bound and zeroes its velocity on that dimension (the default), `reflect` mirrors it back inside and reverses that velocity component, `random` redraws that coordinate uniformly and zeroes its velocity
- Configurable inertia (`w`), cognitive coefficient (`c1`), and social coefficient (`c2`)
- `pbest` initialization by particle position, or by independent random draw

## GA

- Binary genetic algorithm over an N-dimensional continuous search space: each individual carries `n_dims × ind_size` bits, with each dimension's `ind_size`-bit slice linearly decoded onto `[domain_min, domain_max]`
- Four selection operators:
  - `roulette`: fitness-proportional, with weights shifted so the worst individual gets weight 1 (`max − f + 1` when minimizing, `f − min + 1` when maximizing). Works with negative fitness, but the `+1` is in fitness units, so selection pressure depends on the scale of the fitness values
  - `roulette_normalized`: like `roulette`, but the shift past the worst individual is the population's average fitness spacing (`range / (pop_size − 1)`) instead of 1 fitness unit. Pressure doesn't depend on the fitness scale, and the worst individual keeps a small chance of being picked
  - `rank`: linear ranking. Individuals are weighted by their rank alone; `rank_pressure` sets the expected number of copies of the best individual (the worst gets `2 − rank_pressure`)
  - `tournament`: best of `tournament_size` individuals drawn at random
- Two crossover operators:
  - `n_point`: dimension-aware n-point crossover. `cut_points` lists the cut ratios, and they apply independently within each dimension's slice. Every cut must actually split the slice: ratios landing on bit 0, or two ratios landing on the same bit, are rejected because they'd silently yield fewer cuts than listed. Segments between cuts alternate keep / swap, starting with keep, so `[0.6]` is classic single-point and `[0.3, 0.7]` is two-point
  - `uniform`: every bit is exchanged independently with probability 0.5
- Optional family-competition elitism (`elitism = "family"`): after crossover and mutation, each pair keeps the best two of its two parents and two children (children win ties). An unpaired last individual keeps the better of itself and its mutant. Costs two extra fitness evaluations per pair, since parent fitness is reused from selection
- Per-bit mutation at a configurable rate
- `ind_size` capped at 63 so the per-dimension decode fits in a single `uint64_t`

## SMA

- Rank-based weighting: at each iteration slimes are ranked by fitness; the better half gets weights `1 + r·log₁₀((bF − fᵢ)/S + 1)`, the worse half gets `1 − r·log₁₀(…)` — better rank attracts, worse rank repels
- Three-branch position update per slime: with probability `z`, full uniform re-init; otherwise, per-dimension, probability `tanh|fᵢ − DF|` of exploiting around the best-ever using two random peers and amplitude `a = atanh(1 − t/T)`; else contract with amplitude `b = 1 − t/T`
- Decay: both `a` and `b` shrink to zero as `t → T`, trading exploration for exploitation across the run
- Opportunistic best-ever update within the iteration (mirrors PSO's async gbest — later slimes see the fresh best)
- Configurable boundary handling (`bounds`): `clamp` pins a coordinate to the bound (the default), `reflect` mirrors it back inside, `random` redraws it uniformly

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

| Flag       | Long form       | Description                                                |
|------------|-----------------|------------------------------------------------------------|
| `-f FILE`  | `--config FILE` | Path to TOML configuration file (required)                 |
| `-i`       | `--interactive` | Pause after each iteration for inspection                  |
| `-l`       | `--list`        | List available algorithms                                  |
| `-d`       | `--dump-config` | Print the resolved configuration and exit                  |
| `-h [ALG]` | `--help [ALG]`  | General help, or algorithm-specific help if `ALG` is given |

## Configuration

All parameters are set in a TOML file passed via `-f`, split into a `[common]` section for algorithm-agnostic fields and a `[[run]]` section carrying the algorithm selection and its algorithm-specific fields. See [`example.toml`](example.toml) for a fuller example covering every algorithm.

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
- Only two top-level sections are allowed: `[common]` (optional) and `[[run]]` (required). This version runs exactly one `[[run]]`.
- `algorithm` is required and goes in `[[run]]`. It selects the algorithm and loads its defaults before anything else is applied, so its position inside the section doesn't matter.
- `seed` goes in `[common]` only.
- The other runner fields can go in `[common]`, in `[[run]]`, or both. A value in `[[run]]` overrides the one in `[common]`.
- Algorithm fields go in `[[run]]`.
- Keys are written exactly as listed in the tables below, with underscores.
- Each key may appear only once per section. TOML rejects duplicates at parse time.
- Values are typed: `int` fields take integers (`30`, not `30.0`), `float` fields take floats or integers (`5` is read as `5.0`), and `enum` / `name` fields take quoted strings (`"sphere"`).
- Unknown keys, wrongly typed values, and keys in the wrong section are hard errors at startup. Messages name the file, section, and key, e.g. `Error at 'example.toml [[run]]' [swarm-size]: unknown key for algorithm 'pso'`.
- Range and cross-field checks run after the whole file is read: `domain_min < domain_max`, each fitness function's `n_dims` and domain requirements, and each algorithm's own constraints.

### Runner fields

Fields that belong to the runner rather than to any algorithm.

| Key              | Type   | Default    | Description                                                                                                  |
|------------------|--------|------------|--------------------------------------------------------------------------------------------------------------|
| `seed`           | uint32 | generated  | RNG seed. When omitted, one is generated and reported in the final result. `[common]` only                   |
| `direction`      | enum   | `minimize` | `minimize` or `maximize`                                                                                     |
| `n_dims`         | int    | `2`        | Number of dimensions                                                                                         |
| `iterations`     | int    | `100`      | Iteration budget, the hard cap on every run (generations, for GA)                                            |
| `target_fitness` | float  | unset      | Stop early once the current best is at least as good as this value                                           |
| `stagnation`     | int    | `0`        | Stop early after this many consecutive iterations without improving the best-ever; `0` disables it           |
| `domain_min`     | float  | `-5.0`     | Lower bound of each dimension                                                                                |
| `domain_max`     | float  | `5.0`      | Upper bound of each dimension                                                                                |
| `fitness`        | name   | `sphere`   | One of the eight benchmark functions listed above. Some have fixed `n_dims` or stricter domain bounds, enforced at startup |
| `algorithm`      | name   | required   | `pso`, `ga` or `sma`. `[[run]]` only                                                                         |

### PSO fields

| Key            | Type  | Default         | Description                                                                              |
|----------------|-------|-----------------|------------------------------------------------------------------------------------------|
| `swarm_size`   | int   | `30`            | Number of particles                                                                      |
| `w`            | float | `1.0`           | Inertia weight (at the first iteration, when `w_end` is set)                             |
| `w_end`        | float | unset (= `w`)   | Inertia at the last iteration; linear decay from `w`. Unset means constant inertia       |
| `v_max`        | float | `0.0`           | Velocity cap as a fraction of the domain width; `0` disables it                          |
| `c1`           | float | `2.0`           | Cognitive coefficient                                                                    |
| `c2`           | float | `2.0`           | Social coefficient                                                                       |
| `pbest_init`   | enum  | `position`      | `position` (pbest starts at the initial position) or `random` (independent random point) |
| `gbest_update` | enum  | `async`         | `async` (gbest moves as soon as a particle improves it) or `sync` (once per iteration)   |
| `bounds`       | enum  | `clamp`         | Out-of-domain handling: `clamp`, `reflect` or `random`. Reflection bounces as many times as needed for coordinates that overshoot by more than the domain width |

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

| Key        | Type  | Default | Description                                            |
|------------|-------|---------|--------------------------------------------------------|
| `pop_size` | int   | `30`    | Population size                                        |
| `z`        | float | `0.03`  | Full re-init probability per slime, in `[0.0, 1.0]`    |
| `bounds`   | enum  | `clamp` | Out-of-domain handling: `clamp`, `reflect` or `random` |

## Dev environment

The repo includes a Nix flake with the C toolchain, tomlc17, and the tools used during development:

```bash
nix develop
```

enters a shell with `gcc`, `pkg-config`, `tomlc17`, `clang-tools`, `gdb` and `bear`. With [direnv](https://direnv.net) installed, the shell activates automatically on `cd` into the directory: copy `.envrc.example` to `.envrc` and run `direnv allow`.
