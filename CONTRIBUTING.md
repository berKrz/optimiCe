# Contributing

Thanks for your interest in optimiCe. This guide covers setting up a development environment, the conventions the code follows, the tests, and the two most common contributions: adding an algorithm and adding a fitness function. For how the code is put together, read [Architecture](docs/architecture.md) first.

## Setup

The repository includes a Nix flake with everything needed to build and work on optimiCe:

```bash
nix develop
```

enters a shell with `gcc`, `pkg-config`, tomlc17, Criterion, `clang-tools` (clang-format and clangd), `gdb` and `bear`. With [direnv](https://direnv.net) installed, the shell activates automatically on `cd` into the repository: copy `.envrc.example` to `.envrc` and run `direnv allow`.

Without Nix, install tomlc17 as described in the [README](README.md), plus [Criterion](https://github.com/Snaipe/Criterion) for the unit tests and `clang-format` for formatting.

For editor support through clangd, bear records the compile commands in `compile_commands.json`. It only records what actually compiles, so record a full build once, tests included, then append to it as you work:

```bash
make clean && bear -- make all test    # once: every file compiled and recorded
bear --append -- make test             # afterwards: adds whatever was rebuilt
```

A file missing from `compile_commands.json` gets the flags of the most similar file that's in it: a test file then borrows a source file's flags, and clangd reports errors that aren't there.

## Workflow

- Branch from `main`, and open a pull request against it. Changes reach `main` only through pull requests.
- Keep each commit to one change, with a message in the [Conventional Commits](https://www.conventionalcommits.org) style: `feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `build:`, `ci:`, followed by a short summary.
- Before opening a pull request, check that:
  - `make` builds without warnings;
  - `make test` passes, plain and under each sanitizer (see [Sanitizers](#sanitizers));
  - the files you changed are formatted with `clang-format -i`;
  - `make docs` leaves the docs unchanged, or you've committed what it changed.

Every pull request runs three checks: the build, with warnings as errors; `make test`, plain and under each sanitizer; and the docs, which are regenerated and must match what's committed. When the docs check fails, its log shows the exact difference: run `make docs` and commit the result.

## Conventions

- **Formatting** follows `.clang-format`.
- **Comments** all follow one pattern: clear, concise and straight to the point. There are no file-level comments, and no paper references in the source: references belong in the docs.
- **Names** are descriptive, not the symbols of the paper an algorithm comes from: `approach_prob`, not `p`. The docs map the paper's symbols to the code's names where it helps.
- **Configuration** is read from the global `g_cfg` rather than passed around, which keeps signatures short. It is const, so an algorithm can read it but not change it. An algorithm's own parameters live in a config struct private to its file.
- **Validation happens once, up front.** Everything that can be checked is checked while loading; code that runs after validation doesn't re-check what validation guarantees.
- **Nothing is listed by hand that a table already knows.** Help, `--list`, `--dump-config` and the docs' tables are all generated from the field tables, the fitness catalog and the algorithm registry.

## Tests

There are two layers of tests, and `make test` runs both:

- **Unit tests**, in `tests/unit/`: C, written with [Criterion](https://github.com/Snaipe/Criterion), calling the program's functions directly. `make test-unit` runs them alone.
- **Shell tests**, in `tests/cli/`: scripts that run the built `optimice` the way a user would. `make test-cli` runs them alone.

A test goes in the shell layer when what it checks ends the program, an error message and its exit status, or is output: the terminal's lines, `--list`, `--dump-config`, the results file. Everything else goes in the unit layer. [Architecture](docs/architecture.md#tests) explains how both are built.

### Unit tests

Each file covers one area, `tests/unit/test-<area>.c`, and includes only `unittest.h`, or `helpers.h` for the shared helpers: loading a config from a string, running a job as the runner does, reading an algorithm's fields through its table. Criterion runs every test in its own process, so a test can set `g_cfg_rw` and the algorithms' configs freely: nothing carries over to the next test.

To test a function that's private to its file, replace its `static` with `UNITTEST`, and give it a one-line prototype before its definition:

```c
UNITTEST double level_value(uint64_t level);
UNITTEST double level_value(uint64_t level) {
```

`UNITTEST` is `static` in the program and nothing in the build the tests link against. The Makefile collects the prototypes into `build/unittest.h`, so the prototype must fit on one line, and its signature may only use public types: a test can't call a function that takes a struct private to its file.

### Shell tests

Each file covers one area, `tests/cli/test-<area>.sh`, and sources `lib.sh`. A case starts with `begin`, writes its config with `config` or `one_run`, runs `optimice` with `run`, and ends with one check:

```sh
begin "n_dims below 1"
one_run pso 'n_dims = 0'
check
expect_error "Error at 'case.toml [[run]] pso-1': n_dims must be at least 1"
```

Every case runs in a fresh temporary directory, on a config written there as `case.toml`. Error messages are matched exactly; only the part another program writes, such as tomlc17's or the system's text, is left out. A rejected value sits next to the accepted value just inside, read back with `expect_dump`. Results are checked by their structure, never by their numbers, which would change with any change to an algorithm.

### Sanitizers

`SANITIZE` builds and tests an instrumented copy of the program, under its own directory in `build/`, so it never mixes with the normal build. It takes the same list as GCC's `-fsanitize=`, and CI runs two:

```sh
make test SANITIZE=address,undefined    # memory errors, leaks and undefined behavior
make test SANITIZE=thread               # data races
```

A sanitizer stops the program at its first report, with exit status 86, so the case that triggered it fails and shows the report. Under `thread`, the unit tests are skipped with a message, since Criterion can't run under ThreadSanitizer; the shell tests still run the thread pool. Set `ASAN_OPTIONS`, `UBSAN_OPTIONS` or `TSAN_OPTIONS` to override the defaults while debugging.

## Adding an algorithm

An algorithm is a single file, `src/algorithms/<name>.c`, plus its doc and its tests. Nothing else needs to change: the Makefile finds the file and registers the algorithm.

### The code

Every algorithm file has the same sections, in the same order: Config, Field table, Validate, State, Steps, Interactive output, Entry point.

What every algorithm must do:

- **Include only `algorithm.h`.** It's the whole contract an algorithm is written against; calling anything else is a compile error.
- **Name its value `<name>_algorithm`, after its file.** The registry is generated from the file names, and a file without its value fails to link.
- **Declare every parameter in its field table.** `FIELD` and `ENUM_FIELD` take the key from the config struct's member, its default as the user would write it, and a one-line description, which shows up in `--help` and the docs. Floats that can be left unset default to `"nan"`.
- **Check its config in `validate`,** with `die_with_hint` for every invalid value. With a `bounds` field, call `check_bounds_mode`; for buffers sized from the config, `check_fits_int`.
- **Evaluate only through `evaluate`, and every candidate exactly once:** keep the fitness of what has been evaluated, and never evaluate again to compare or to print. The count is the run's reported cost.
- **Draw randomness only from the RNG functions,** never `rand()` or another source: they're what make a job reproducible from its seed.
- **Loop with `for (;;)` and call `end_iteration` once at the end of every iteration** with the current best fitness, leaving the loop when it returns 1. It handles progress lines and every stopping rule; the budget's caps and counters are off limits (algorithm files can't even name `budget`, `iterations`, `evaluations` or `evaluation_cutoff`, which are poisoned there).
- **Pace any schedule with `progress`,** the share of the run's budget used, read before the iteration's moves. It's in (0, 1], so it's never 0 on the first iteration; a schedule that must start exactly at its initial value measures from the first iteration's progress, as PSO's inertia does.
- **Fill in the result:** the best fitness, its position in `out->pos`, which holds `n_dims` values, and the `StopState`.
- **Print only under `--interactive`,** using the shared helpers in `algorithm.h`, and end each view of the population with a legend for its markers.
- **Keep larger state in a struct** with allocation and free functions, like PSO's `Swarm`.

### The doc

Every algorithm needs `docs/algorithms/<name>.md`, and `make docs` fails until it exists. Follow the shape of the existing ones:

1. A `# ` title, `<ACRONYM> - <Full name>`, which also becomes the algorithm's name in the generated lists.
2. An introduction: what the algorithm does, which variant this is, and what the run reports as its result.
3. **How it works:** initialization and each iteration, in words first, then the equations.
4. **Options:** one subsection per behavior a field controls.
5. **Fields:** the generated table, then the checks `validate` makes.
6. **Example:** a short configuration that runs.
7. **References.**

For the Fields section, write an empty block and let `make docs` fill it:

```markdown
<!-- gen:fields <name> -->
<!-- /gen -->
```

### The tests

Every algorithm needs `tests/unit/test-<name>.c` and `tests/cli/test-<name>.sh`, and `make test` fails until both exist.

What every algorithm is checked for already runs without them: `tests/unit/test-algorithms.c` reads the registry and the field tables, and runs each algorithm under every combination of its enum fields for determinism and a sound result, on several threads at once, and with each early stop. The algorithm's own files cover what's specific to it:

- **Unit:** its evaluation count, the exact formula for a run of a given length; and its own functions, marked `UNITTEST`, where they have edge cases worth pinning down.
- **Shell:** every check its `validate` makes, each rejected value next to the accepted value just inside.

## Adding a fitness function

1. **Write the function** in `src/core/fitness.c`, as a `static` function taking the position and reading the number of dimensions from `g_cfg.n_dims`. It must not return NaN at a finite point of its domain, since nothing compares better or worse than NaN; overflowing to an infinity is fine.
2. **Add its row to the catalog** in the same file: its config name, the function, its limits on `n_dims` (`0` for no limit), and the domain it's defined over (`-INFINITY` and `INFINITY` for none).
3. **Document it** with a `## <name>` section in [`docs/fitness.md`](docs/fitness.md): a sentence or two on its character, then its formula as the code computes it. `make docs` fails until the section exists, and adds the function to the catalog table.
4. **Test it** in `tests/unit/test-fitness.c`: its value at its known optimum, if it has one, and at a point or two worked out by hand. The check that no function returns NaN in its domain already covers it, through the catalog.

## Generated docs

Parts of the docs are generated from the source: field tables, the fitness catalog and the algorithm list. They sit between `<!-- gen:... -->` and `<!-- /gen -->` markers, and `make docs` rewrites them. Edit the source tables, not the generated text; anything outside the markers is yours to write. [Architecture](docs/architecture.md#docs) explains how it works.
