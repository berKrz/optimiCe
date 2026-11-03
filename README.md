# optimiCe

A runner for metaheuristic optimization algorithms, written in C.

optimiCe runs an optimization algorithm against a user-defined fitness function and reports the best solution it finds. The algorithm, fitness function, and all parameters are specified in an INI configuration file.

## Common

- Eight standard benchmark fitness functions: `sphere`, `rosenbrock`, `rastrigin`, `griewank`, `ackley`, `eggholder`, `schwefel`, `schaffer_f6`
- N-dimensional continuous search space
- Minimization or maximization
- Reproducible runs: `seed` can be set in the configuration; when omitted, one is generated and printed in the final result so any run can be rerun exactly
- Configuration errors caught at startup, with messages pointing at the offending `file:line` and key
- Interactive step-through mode (`-i`) for inspection
- `--dump-config` prints the resolved configuration for debugging

## PSO

- Classical velocity update: `v ← w·v + c₁·r₁·(pbest − x) + c₂·r₂·(gbest − x)`
- Asynchronous gbest update: gbest moves within the iteration as soon as any particle improves it
- Boundary clamping with velocity reset: particles pushed outside the domain are pinned to the bound and their velocity on that dimension zeroed
- Configurable inertia (`w`), cognitive coefficient (`c1`), and social coefficient (`c2`)
- `pbest` initialization by particle position, or by independent random draw

## Build

```bash
make
```

Requires `gcc` and `make`. Produces the `optimice` binary in the repo root.

## Usage

```bash
./optimice -f example.ini
```

| Flag        | Long form           | Description                                              |
|-------------|---------------------|----------------------------------------------------------|
| `-f FILE`   | `--config FILE`     | Path to INI configuration file (required)                |
| `-i`        | `--interactive`     | Pause after each iteration for inspection                |
| `-l`        | `--list`            | List available algorithms                                |
| `-d`        | `--dump-config`     | Print the resolved configuration and exit                |
| `-h [ALG]`  | `--help [ALG]`      | General help, or algorithm-specific help if `ALG` is given |

## Configuration

All parameters are set in an INI file passed via `-f`.

```ini
# optimice example config - minimal PSO run
algorithm = pso

n_dims    = 2
fitness   = sphere
direction = minimize
domain_min = -5.0
domain_max =  5.0
seed       = 42

# PSO-specific
swarm_size = 30
iterations = 100
w          = 1.0
c1         = 2.0
c2         = 2.0
pbest_init = position
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
| `algorithm`  | enum   | required   | `pso`                                                                                           |
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

## Dev environment

The repo includes a Nix flake with the C toolchain and tools used during development:

```bash
nix develop
```

enters a shell with `gcc`, `clang-tools`, and `gdb`. With [direnv](https://direnv.net) installed, the shell activates automatically on `cd` into the directory - copy `.envrc.example` to `.envrc` and run `direnv allow`.

Nix is optional. `gcc` + `make` is enough to build and run the binary.
