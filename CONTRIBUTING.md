# Contributing

Thanks for your interest in optimiCe. This guide covers setting up a development environment, the conventions the code follows, and the two most common contributions: adding an algorithm and adding a fitness function. For how the code is put together, read [Architecture](docs/architecture.md) first.

## Setup

The repository includes a Nix flake with everything needed to build and work on optimiCe:

```bash
nix develop
```

enters a shell with `gcc`, `pkg-config`, tomlc17, `clang-tools` (clang-format and clangd), `gdb` and `bear`. With [direnv](https://direnv.net) installed, the shell activates automatically on `cd` into the repository: copy `.envrc.example` to `.envrc` and run `direnv allow`.

Without Nix, install tomlc17 as described in the [README](README.md), plus `clang-format` for formatting.

For editor support through clangd, `bear -- make` records the compile commands in `compile_commands.json`; run `make clean` first, so every file is compiled and recorded.

## Workflow

- Branch from `main`, and open a pull request against it. Changes reach `main` only through pull requests.
- Keep each commit to one change, with a message in the [Conventional Commits](https://www.conventionalcommits.org) style: `feat:`, `fix:`, `refactor:`, `docs:`, `build:`, `ci:`, followed by a short summary.
- Before opening a pull request, check that:
  - `make` builds without warnings;
  - the files you changed are formatted with `clang-format -i`;
  - `make docs` leaves the docs unchanged, or you've committed what it changed.

Every pull request runs a check that regenerates the docs and fails if they differ from what's committed. When it fails, its log shows the exact difference: run `make docs` and commit the result.

## Conventions

- **Formatting** follows `.clang-format`.
- **Comments** all follow one pattern: clear, concise and straight to the point. There are no file-level comments, and no paper references in the source: references belong in the docs.
- **Names** are descriptive, not the symbols of the paper an algorithm comes from: `approach_prob`, not `p`. The docs map the paper's symbols to the code's names where it helps.
- **Configuration** is read from the global `g_cfg` rather than passed around, which keeps signatures short. An algorithm's own parameters live in a config struct private to its file.
- **Validation happens once, up front.** Everything that can be checked is checked while loading; code that runs after validation doesn't re-check what validation guarantees.
- **Nothing is listed by hand that a table already knows.** Help, `--list`, `--dump-config` and the docs' tables are all generated from the field tables, the fitness catalog and the algorithm registry.

## Adding an algorithm

An algorithm is a single file, `src/algorithms/<name>.c`, plus its doc. Nothing else needs to change: the Makefile finds the file and registers the algorithm.

### The code

Every algorithm file has the same sections, in the same order: Config, Field table, Validate, State, Steps, Interactive output, Entry point.

What every algorithm must do:

- **Include only `algorithm.h`.** It's the whole contract an algorithm is written against; calling anything else is a compile error.
- **Name its value `<name>_algorithm`, after its file.** The registry is generated from the file names, and a file without its value fails to link.
- **Declare every parameter in its field table.** `FIELD` and `ENUM_FIELD` take the key from the config struct's member, its default as the user would write it, and a one-line description, which shows up in `--help` and the docs. Floats that can be left unset default to `"nan"`.
- **Check its config in `validate`,** with `die_with_hint` for every invalid value. With a `bounds` field, call `check_bounds_mode`; for buffers sized from the config, `check_fits_int`.
- **Evaluate only through `evaluate`, and every candidate exactly once:** keep the fitness of what has been evaluated, and never evaluate again to compare or to print. The count is the run's reported cost.
- **Draw randomness only from the RNG functions,** never `rand()` or another source: they're what make a job reproducible from its seed.
- **Call `end_iteration` once at the end of every iteration** with the current best fitness, and leave the loop when it returns 1. It handles progress lines and early stopping.
- **Fill in the result:** the best fitness, its position in `out->pos`, which holds `n_dims` values, and the `StopState`.
- **Print only under `--interactive`,** using the shared helpers in `algorithm.h`, and end each view of the population with a legend for its markers.
- **Keep larger state in a struct** with allocation and free functions, like PSO's `Swarm`, and use `i` for the iteration loop's variable.

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

## Adding a fitness function

1. **Write the function** in `src/core/fitness.c`, as a `static` function taking the position and reading the number of dimensions from `g_cfg.n_dims`. It must not return NaN for finite coordinates, since nothing compares better or worse than NaN; overflowing to an infinity is fine.
2. **Add its row to the catalog** in the same file: its config name, the function, its limits on `n_dims` (`0` for no limit), and the domain it's defined over (`-INFINITY` and `INFINITY` for none).
3. **Document it** with a `## <name>` section in [`docs/fitness.md`](docs/fitness.md): a sentence or two on its character, then its formula as the code computes it. `make docs` fails until the section exists, and adds the function to the catalog table.

## Generated docs

Parts of the docs are generated from the source: field tables, the fitness catalog and the algorithm list. They sit between `<!-- gen:... -->` and `<!-- /gen -->` markers, and `make docs` rewrites them. Edit the source tables, not the generated text; anything outside the markers is yours to write. [Architecture](docs/architecture.md#docs) explains how it works.
