# optimiCe

A runner for metaheuristic optimization algorithms, written in C.

optimiCe runs an optimization algorithm against a user-defined fitness function and reports the best solution it finds. The algorithm, fitness function, and all parameters are specified in a TOML configuration file.

## Common

- Eight standard benchmark fitness functions: `sphere`, `rosenbrock`, `rastrigin`, `griewank`, `ackley`, `eggholder`, `schwefel`, `schaffer_f6`
- N-dimensional continuous search space
- Minimization or maximization
- Reproducible runs: `seed` can be set in the configuration; when omitted, one is generated and printed in the final result so any run can be rerun exactly
- Configuration errors caught at startup, with messages pointing at the offending section and key
- Interactive step-through mode (`-i`) for inspection
- `--dump-config` prints the resolved configuration for debugging

## PSO

- Classical velocity update: `v ← w·v + c₁·r₁·(pbest − x) + c₂·r₂·(gbest − x)`
- Asynchronous gbest update: gbest moves within the iteration as soon as any particle improves it
- Boundary clamping with velocity reset: particles pushed outside the domain are pinned to the bound and their velocity on that dimension zeroed
- Configurable inertia (`w`), cognitive coefficient (`c1`), and social coefficient (`c2`)
- `pbest` initialization by particle position, or by independent random draw

## GA

- Binary genetic algorithm over an N-dimensional continuous search space: each individual carries `n_dims × ind_size` bits, with each dimension's `ind_size`-bit slice linearly decoded onto `[domain_min, domain_max]`
- Fitness-proportional (roulette) or tournament selection; roulette shifts weights by `max − f + 1` under minimization so the direction-agnostic draw stays meaningful
- Dimension-aware single-point crossover: the cut ratio applies independently within each dimension's slice, so no dimension is wholly swapped at once
- Per-bit mutation at a configurable rate
- `ind_size` capped at 63 so the per-dimension decode fits in a single `uint64_t`

## SMA

- Rank-based weighting: at each iteration slimes are ranked by fitness; the better half gets weights `1 + r·log₁₀((bF − fᵢ)/S + 1)`, the worse half gets `1 − r·log₁₀(…)` — better rank attracts, worse rank repels
- Three-branch position update per slime: with probability `z`, full uniform re-init; otherwise, per-dimension, probability `tanh|fᵢ − DF|` of exploiting around the best-ever using two random peers and amplitude `a = atanh(1 − t/T)`; else contract with amplitude `b = 1 − t/T`
- Decay: both `a` and `b` shrink to zero as `t → T`, trading exploration for exploitation across the run
- Opportunistic best-ever update within the iteration (mirrors PSO's async gbest — later slimes see the fresh best)
- Boundary clamping: positions driven outside the domain are pinned to the bound


## Build

```bash
make
```

Produces the `optimice` binary in the repo root. The Nix shell provides `gcc`, `make`, `clang-tools` (clangd), `gdb`, `bear`, and the `tomlc17` dependency. With [direnv](https://direnv.net) installed, the shell activates automatically on `cd` into the directory — copy `.envrc.example` to `.envrc` and run `direnv allow`.

## Usage

```bash
./optimice -f example.toml
```

| Flag        | Long form           | Description                                              |
|-------------|---------------------|----------------------------------------------------------|
| `-f FILE`   | `--config FILE`     | Path to TOML configuration file (required)                |
| `-i`        | `--interactive`     | Pause after each iteration for inspection                |
| `-l`        | `--list`            | List available algorithms                                |
| `-d`        | `--dump-config`     | Print the resolved configuration and exit                |
| `-h [ALG]`  | `--help [ALG]`      | General help, or algorithm-specific help if `ALG` is given |

## Configuration

All parameters are set in a TOML file passed via `-f`, split into a `[common]` section for algorithm-agnostic fields and a `[[run]]` section carrying the algorithm selection and its algorithm-specific fields.

```ini
# optimice example config - minimal PSO run
[common]
n_dims     = 2
fitness    = "sphere"
direction  = "minimize"
domain_min = -5.0
domain_max =  5.0
seed       = 42

# PSO-specific
[[run]]
algorithm  = "pso"
swarm_size = 30
iterations = 100
w          = 1.0
c1         = 2.0
c2         = 2.0
pbest_init = "position"
```

### Syntax

- Lines starting with `#` are comments. Blank lines are ignored.
- Keys accept either `-` or `_` as a separator: `swarm_size` and `swarm-size` are equivalent.
- `algorithm` must be the first non-comment key and must appear exactly once. Setting it loads that algorithm's defaults and unlocks its fields.
- All other keys follow last-write-wins: repeating a key overrides the earlier value.
- Unknown keys produce a hard error at startup, naming the `file:line` and key.

### Common fields

| Key          | Type   | Default    | Description                                                                                     |
|--------------|--------|------------|-------------------------------------------------------------------------------------------------|
| `algorithm`  | enum   | required   | `pso`, `ga` or `sma`                                                                                   |
| `seed`       | uint   | auto       | RNG seed (32-bit). When omitted, one is generated and reported in the final result               |
| `direction`  | enum   | `minimize` | `minimize` or `maximize`                                                                        |
| `n_dims`     | int    | `2`        | Number of dimensions                                                                            |
| `domain_min` | float  | `-5.0`     | Lower bound of each dimension                                                                   |
| `domain_max` | float  | `5.0`      | Upper bound of each dimension                                                                   |
| `fitness`    | name   | `sphere`   | One of the eight benchmark functions listed above. Some have fixed `n_dims` or stricter domain bounds, enforced at startup |

### PSO fields

| Key          | Type  | Default    | Description                                                                           |
|--------------|-------|------------|---------------------------------------------------------------------------------------|
| `swarm_size` | int   | `30`       | Number of particles                                                                   |
| `iterations` | int   | `100`      | Number of iterations                                                                  |
| `w`          | float | `1.0`      | Inertia weight                                                                        |
| `c1`         | float | `2.0`      | Cognitive coefficient                                                                 |
| `c2`         | float | `2.0`      | Social coefficient                                                                    |
| `pbest_init` | enum  | `position` | `position` (pbest starts at the initial position) or `random` (independent random point) |

### GA fields

| Key               | Type  | Default        | Description                                                                                 |
|-------------------|-------|----------------|---------------------------------------------------------------------------------------------|
| `pop_size`        | int   | `15`           | Population size (minimum 2)                                                                 |
| `ind_size`        | int   | `22`           | Bits per dimension, in `[2, 63]`. Total bits per individual is `n_dims × ind_size`          |
| `generations`     | int   | `30`           | Number of generations                                                                       |
| `tournament_size` | int   | `2`            | Individuals per tournament; must be at most `pop_size`. Only used when `selection=tournament` |
| `cut_point`       | float | `0.6`          | Cut point ratio in `(0.0, 1.0)`; the cut index is `floor(ind_size × cut_point)` per dimension |
| `mutation_rate`   | float | `0.01`         | Per-bit flip probability in `[0.0, 1.0)`                                                    |
| `selection`       | name  | `roulette`     | `roulette` or `tournament`                                                                  |
| `crossover`       | name  | `single_point` | `single_point` (only option so far)                                                         |

### SMA fields

| Key          | Type  | Default | Description                                         |
|--------------|-------|---------|-----------------------------------------------------|
| `pop_size`   | int   | `30`    | Population size                                     |
| `iterations` | int   | `100`   | Number of iterations                                |
| `z`          | float | `0.03`  | Full re-init probability per slime, in `[0.0, 1.0]` |


## Dev environment

The repo includes a Nix flake with the C toolchain and tools used during development:

```bash
nix develop
```

enters a shell with `gcc`, `clang-tools`, `gdb` and `bear`. With [direnv](https://direnv.net) installed, the shell activates automatically on `cd` into the directory - copy `.envrc.example` to `.envrc` and run `direnv allow`.

Nix is optional. `gcc` + `make` is enough to build and run the binary.
