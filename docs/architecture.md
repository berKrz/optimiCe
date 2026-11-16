# Architecture

How optimiCe is put together: where the code lives, how a configuration file becomes results, and the design decisions that keep runs reproducible and algorithms easy to add. For adding an algorithm or a fitness function step by step, see [CONTRIBUTING](../CONTRIBUTING.md).

## Source layout

```
src/
  main.c         the pipeline, from the command line to the results
  algorithms/    one file per algorithm, plus algorithm.h, the contract they're written against
  core/          what every run shares: fitness catalog, bounds, stopping, ranking, RNG,
                 algorithm registry, batch runner, errors
  config/        the field engine and the runner's own fields
  io/            command line, config loading, terminal output, results file
tools/
  gen-docs.c     regenerates the docs' tables from the source tables
build/           everything the build produces: objects, dependency files, generated headers
```

Each directory has a single header: `algorithms/algorithm.h`, `core/core.h`, `config/config.h` and `io/io.h`. They build on each other, from the narrowest to the widest: `core.h` includes `algorithm.h`, and `io.h` includes `core.h`.

## Boundaries

An algorithm file includes only `algorithm.h`, and the build treats calling an undeclared function as an error (`-Werror=implicit-function-declaration`). So `algorithm.h` is exactly what an algorithm can use, and the compiler enforces it:

- the run's configuration, `g_cfg`, read-only outside the runner: assigning to it doesn't compile, and algorithm files can't name its writable version, `g_cfg_rw`;
- `evaluate`, the only way to reach the fitness function;
- `is_better`, the run's direction as a comparison;
- the random number functions;
- `end_iteration`, which handles progress output and the early stops;
- ranking, boundary handling and the shared pieces of the interactive view;
- the field-table macros an algorithm declares its parameters with;
- the error and allocation helpers.

What only the runner may do lives in `core.h`, out of an algorithm's reach: seeding the RNG, resetting the evaluation counter, finding algorithms in the registry, running a batch.

The fitness catalog is also closed: `FitnessEntry` is opaque outside `fitness.c`, so no file can reach a function pointer and call a fitness function without going through `evaluate`.

## The pipeline

`main` reads as the whole program:

1. `parse_args` reads the command line. It answers `--help` and `--list` itself and exits; otherwise it returns what was asked for.
2. `load_batch` parses the TOML file, merges every run's configuration and validates it. Any problem stops here, before anything runs.
3. `--interactive` is checked against the batch's size: it needs a single job.
4. With `--dump-config`, the resolved batch is printed and the program exits.
5. Otherwise the results file is opened if `-o` was given, the batch runs, and the file is closed.

## Configuration: field tables

Every parameter, the runner's and each algorithm's, is a row in a field table. A row holds the key, its type, the sections it may appear in, where its value is stored (an offset into a config struct), the choices of an enum, its default as the user would write it, and a one-line description.

One engine in `config/fields.c` drives everything from those rows:

- parsing a TOML value into the struct, with type and section checks;
- setting defaults, by parsing each row's default text exactly like a value from a file;
- `--help`, `--dump-config`, and the docs' generated tables.

So a parameter is declared once, and its name, default, documentation and checks can't disagree. The row macros take the key from the struct member's name, so the key and the member can't disagree either.

The runner's table writes into `Config`, the struct every run has. Each algorithm's table writes into its own config struct, private to its file and reached through the algorithm's `cfg_base`.

### Loading

`load_batch` merges each run from two independent precedence chains, since `[common]` can't hold algorithm fields and `[defaults.<algo>]` can't hold runner fields:

```
runner fields:     defaults -> [common]          -> [[run]]
algorithm fields:  defaults -> [defaults.<algo>] -> [[run]]
```

Each chain's base is built once. Every run starts from fresh copies of both bases, applies its own keys, and is validated: first the runner's checks, then its algorithm's `validate`, which may also resolve derived values, so the dump and the run see the same final configuration. The result is a `RunConfig` per run, the runner config plus a copy of the algorithm's config, and the `Batch` that holds them is read-only from then on.

## Algorithms

An algorithm is a `const Algorithm` value:

- `name`: its config name.
- `fields`: its field table.
- `cfg_base` and `cfg_size`: where its config lives, and how big it is. The config is copied by value, so it must hold its data by value.
- `validate`: checks its config, dying on the first problem, and may resolve derived values.
- `run`: runs one job, filling in the best fitness, its position and how the run ended.

A run evaluates through `evaluate`, draws random numbers through the RNG functions, and calls `end_iteration` once per iteration with its current best. It prints nothing except under `--interactive`.

There is no list of algorithms to maintain. The Makefile lists `src/algorithms/*.c`, and for each file `<name>.c` it expects a `<name>_algorithm` value. From those names it generates `build/registry.h`, which declares every algorithm and defines `algorithms[]`, sorted alphabetically. Adding a file adds an algorithm.

## Running a batch

A batch has `runs × replicates` jobs. Job `j` is run `j / replicates`, replicate `j % replicates`, and runs with seed `seed + replicate`.

### Per-thread state

Everything a run reads or writes while it runs is thread-local: `g_cfg`, each algorithm's config, the RNG state, the evaluation counter, and `g_where`, the label errors are reported against. To run a job, a thread copies the run's two configs into its own, sets the job's seed, seeds its RNG, resets its counter, and calls the algorithm's `run`. Jobs never share mutable state, so they never wait for each other.

### The worker pool

Jobs run on C11 threads, `min(jobs, CPU cores)` of them; more threads than cores would only add switching overhead for CPU-bound jobs. Every job is known up front, so the pool is just an atomic counter: each worker takes the next job number, runs it, and takes another until none remain.

The one shared thing is the printer, behind a mutex. Jobs finish in any order, but results must come out in job order. The printer marks each finished job and prints every result it can reach: a job's line, and its row in the results file, come out once it and every job before it have finished.

A single job skips the pool and runs on the main thread, with per-iteration progress and the full result.

## Reproducibility

A job's result depends only on its seed and its configuration:

- **Random numbers.** Each thread has its own xoshiro256\*\* generator, seeded from the job's 32-bit seed through splitmix64. No job draws from another's stream, whatever the scheduling.
- **No hidden order dependence.** Where a result could depend on an arbitrary order, it's fixed: ranking breaks fitness ties by index, so the order never depends on the sorting algorithm.
- **Output.** Results are printed and written in job order, and what depends on the machine, the thread count, goes to stderr. Stdout and the results file, apart from its date, are the same on any machine.

Replicate `r` of every run uses the same seed, `seed + r`: the common random numbers design described in [Configuration](configuration.md#seeds-and-reproducibility).

## Fitness evaluation

`evaluate` is the only way to reach a fitness function, and every call increments the thread's evaluation counter. The runner resets the counter before each job and reads it after; an algorithm can do neither, since both live in `core.h`. Algorithms keep the fitness of what they have evaluated and never evaluate a solution twice, so the count is the run's real cost. See [Fitness functions](fitness.md#evaluations).

## Errors

Two error functions cover everything:

- `die`, for failures at run time, such as running out of memory or an unwritable file, and for internal errors.
- `die_with_hint`, for mistakes in what the user wrote. It adds a line pointing at `--help`. `die_at` is its variant for config files, naming the file, section and key.

Both prefix the message with `g_where` when it's set: the run being loaded, or the job a thread is running. When several threads fail at once, the first one to fail reports and exits; the others wait for the process to end, so exactly one error is printed.

Everything that can be checked is checked while loading, so failures during a batch are limited to what no configuration can predict.

## Output

`io/report.c` prints to the terminal; `io/output.c` writes the results file. The file's header records the commit the binary was built from, which the Makefile writes into `build/version.h` from `git describe`, and the config file's POSIX `cksum`, so a results file can be traced back to the code and configuration that produced it. Rows are written by the printer, in job order, and flushed one by one. Final values are printed in the shortest form that reads back exactly, so printed results identify the doubles the run produced.

## Build

The build is a single Makefile, written so that adding a source file, an algorithm or a tool never means editing it. Installing the dependencies and running `make` is covered in the [README](../README.md); this section is about how the build works.

### Targets

- `make` (`all`): builds the `optimice` binary at the top of the repository.
- `make docs`: builds the docs generator and runs it over every doc. It doesn't build `optimice`.
- `make clean`: removes `build/` and the binary.

### Sources

Every `.c` file in `src/` and in its directories, one level down, is compiled; a new source file needs no entry. Objects go under `build/`, mirroring `src/`: `src/core/rng.c` becomes `build/core/rng.o`. Everything the build produces lives there, so `make clean` is a single `rm -rf`, and the source tree never holds build output.

### Flags

Flags come in two groups:

- `CFLAGS` is meant to be overridden, e.g. `make CFLAGS="-O0 -g"` for debugging. It defaults to `-O2 -g -Wall -Wextra`.
- `BUILD_FLAGS` holds what the build needs whatever `CFLAGS` says, so overriding `CFLAGS` can't break it:
  - `-std=c11`, and `-pthread` for the worker threads;
  - `-I src -I build`, so headers are included by their path from `src/`, like `"core/core.h"`, and generated headers are found in `build/`;
  - `-D_GNU_SOURCE`, for the POSIX and GNU functions the code uses;
  - `-MMD -MP`, for dependency tracking;
  - `-finput-charset=UTF-8 -fexec-charset=UTF-8`, since sources and their strings are UTF-8;
  - `-Werror=implicit-function-declaration`, which turns a call to an undeclared function into an error: it's what makes the headers an enforced boundary (see [Boundaries](#boundaries)).

`CC` is left to make's default, so `make CC=clang` works. tomlc17's flags come from `pkg-config` (`PKG_CONFIG` can point elsewhere); when `pkg-config` is missing or doesn't know the library, the build falls back to a plain `-ltomlc17`, for a library installed on a default search path.

### Dependency tracking

`-MMD` makes the compiler write, next to each object, a `.d` file listing every header the object included, and the Makefile includes those files. Changing a header rebuilds exactly the objects that use it. `-MP` adds an empty rule for each header, so deleting or renaming a header doesn't leave make asking for a file that no longer exists.

On a first build there are no `.d` files yet, so make can't know that some objects need a generated header before they compile. Two rules say so explicitly: `output.o` needs `version.h`, and `registry.o` needs `registry.h`.

### Generated headers

Two headers are written by the Makefile into `build/`:

- `version.h` defines the build's version for the results file's header: `git describe --always --dirty`, so the short commit hash, with `-dirty` when the tree has uncommitted changes, or a tag once one exists. Without git it's `unknown`. `VERSION` can be set on the command line instead, which the Nix build does.
- `registry.h` declares every algorithm and defines `algorithms[]`. The Makefile lists the files in `src/algorithms/`, sorts their names, and for each `<name>.c` declares `<name>_algorithm`. An algorithm file that doesn't define its value fails at link time.

Both are regenerated on every `make`, since they depend on things make can't see as files, such as the commit and the contents of a directory. Each is written to a temporary file first and only replaces the real one when the content differs, so its timestamp, and with it every object that includes it, only changes when something actually changed.

### Tools

Every `.c` file in `tools/` is a separate program, built into `build/tools/` and linked against every object of the program except `main.o`, so a tool can reuse the program's tables and functions without being part of the `optimice` binary. Tools are built on demand, by the targets that need them, never by `make` alone; today there is `gen-docs`, built by `make docs`.

### Nix

The flake provides two things:

- A dev shell, `nix develop`, with the compiler, `pkg-config`, tomlc17, `clang-tools` (clang-format and clangd), `gdb`, and `bear`, which records the compile commands clangd reads.
- A package, `nix build`, which runs the same Makefile. A Nix build has no `.git`, so the flake passes the commit in as `VERSION`.

The docs workflow uses the dev shell too, so CI builds with the same toolchain as a local build.

## Docs

The docs' tables come from the source. `tools/gen-docs.c` is linked against every object but `main.o`, so it reuses the field tables, the fitness catalog and the registry. `make docs` runs it over every doc and replaces the content between `<!-- gen:... -->` and `<!-- /gen -->` markers; the text outside them is never touched. Before regenerating anything, it checks that every algorithm has a doc and every fitness function a section in [Fitness functions](fitness.md), so neither can be added undocumented.

A GitHub Actions workflow runs `make docs` on every pull request and every push to `main`, and fails if any doc changes: the committed docs always match the code.
