# The config file's structure: sections, runs, algorithms, keys and where they may go
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

# --- The file ---

begin "missing file"
run -d -f case.toml
expect_error_start "Error: parse error in 'case.toml': "

begin "TOML syntax error"
config <<'TOML'
[common
TOML
run -d -f case.toml
expect_error_start "Error: parse error in 'case.toml': "

# --- Top-level sections ---

begin "unknown top-level key"
config <<'TOML'
[foo]
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml' [foo]: unknown top-level key (only [common], [defaults.<algo>] and [[run]] are allowed)"

begin "common isn't a table"
config <<'TOML'
common = 1
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml' [common]: must be a table"

begin "defaults isn't a table"
config <<'TOML'
defaults = 1
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml' [defaults]: must be a table of [defaults.<algo>] sections"

# --- Runs ---

begin "no run"
config <<'TOML'
[common]
n_dims = 3
TOML
run -d -f case.toml
expect_error "Error at 'case.toml': missing [[run]] section"

begin "run isn't a table"
config <<'TOML'
run = [1]
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] #1': [[run]] entry must be a table"

begin "run without an algorithm"
config <<'TOML'
[[run]]
n_dims = 3
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] #1': [[run]] is missing 'algorithm'"

begin "algorithm isn't a string"
config <<'TOML'
[[run]]
algorithm = 1
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] #1' [algorithm]: expected string"

begin "unknown algorithm"
config <<'TOML'
[[run]]
algorithm = "nope"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] #1' [algorithm]: unknown algorithm"

# --- [defaults.<algo>] ---

begin "defaults for an unknown algorithm"
config <<'TOML'
[defaults.nope]
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [defaults]' [nope]: unknown algorithm"

begin "defaults entry isn't a table"
config <<'TOML'
[defaults]
pso = 1
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [defaults]' [pso]: must be a table"

# --- Replicates ---

begin "replicates below 1"
config <<'TOML'
[common]
replicates = 0
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [replicates]: must be at least 1"

begin "replicates of 1"
config <<'TOML'
[common]
replicates = 1
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_dump replicates 1

begin "too many jobs"
config <<'TOML'
[common]
replicates = 2147483647
[[run]]
algorithm = "pso"
[[run]]
algorithm = "ga"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [replicates]: too many jobs (runs x replicates)"

begin "as many jobs as fit"
config <<'TOML'
[common]
replicates = 2147483647
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_dump replicates 2147483647

# --- Unknown keys ---

begin "unknown key in [common]"
config <<'TOML'
[common]
nope = 1
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [nope]: unknown key"

begin "unknown key in a run"
config <<'TOML'
[[run]]
algorithm = "pso"
nope = 1
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] pso-1' [nope]: unknown key for algorithm 'pso'"

begin "unknown key in [defaults.pso]"
config <<'TOML'
[defaults.pso]
nope = 1
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [defaults.pso]' [nope]: unknown key for algorithm 'pso'"

# --- Keys in the wrong section ---

begin "[common]-only key in a run"
config <<'TOML'
[[run]]
algorithm = "pso"
seed = 1
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] pso-1' [seed]: only allowed in [common]"

begin "algorithm key in [common]"
config <<'TOML'
[common]
swarm_size = 10
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [swarm_size]: 'swarm_size' is a field of algorithm 'pso'; algorithm fields go in [[run]] or [defaults.<algo>]"

begin "runner key in [defaults.pso]"
config <<'TOML'
[defaults.pso]
n_dims = 3
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_error "Error at 'case.toml [defaults.pso]' [n_dims]: only allowed in [common] or [[run]]"

finish
