# Field types: each rejected value next to the accepted value just inside, read back from
# --dump-config. There are no bool fields to test
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

# A [common] section holding $1 and an iteration budget of 5, then one pso run
common() {
  printf '[common]\niterations = 5\n%s\n[[run]]\nalgorithm = "pso"\n' "$1" | config
}

# --- int (stagnation) ---

begin "int given a string"
common 'stagnation = "10"'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [stagnation]: expected integer"

begin "int given a float"
common 'stagnation = 2.0'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [stagnation]: expected integer"

begin "int above INT_MAX"
common 'stagnation = 2147483648'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [stagnation]: out of int range"

begin "int at INT_MAX"
common 'stagnation = 2147483647'
run -d -f case.toml
expect_dump stagnation 2147483647

begin "int below INT_MIN"
common 'stagnation = -2147483649'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [stagnation]: out of int range"

# --- uint (seed) ---

begin "uint below 0"
common 'seed = -1'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [seed]: out of uint32 range"

begin "uint above 2^32 - 1"
common 'seed = 4294967296'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [seed]: out of uint32 range"

begin "uint at 2^32 - 1"
common 'seed = 4294967295'
run -d -f case.toml
expect_dump seed 0xffffffff

# --- float (domain_min, domain_max) ---

begin "float given a string"
common 'domain_min = "-5"'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [domain_min]: expected a number"

begin "float given an integer"
common 'domain_min = -4'
run -d -f case.toml
expect_dump domain_min -4

begin "nan for a required float"
common 'domain_min = nan'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [domain_min]: nan is not a valid value"

begin "inf for a domain end"
common "$(printf 'domain_max = inf\ninit_max = 5.0')"
run -d -f case.toml
expect_dump domain_max inf

# --- enum (direction) ---

begin "enum given an unknown value"
common 'direction = "sideways"'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [direction]: unrecognized value"

begin "enum given a non-string"
common 'direction = 1'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [direction]: expected string"

# --- fitness ---

begin "fitness given an unknown name"
common 'fitness = "nope"'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [fitness]: unknown fitness function"

begin "fitness given a non-string"
common 'fitness = 1'
run -d -f case.toml
expect_error "Error at 'case.toml [common]' [fitness]: expected string"

# --- list (cut_points) ---

# A ga run with crossover $1 and cut_points = $2
ga_cuts() {
  printf '[[run]]\nalgorithm = "ga"\niterations = 5\ncrossover = "%s"\ncut_points = %s\n' "$1" "$2" | config
}

# n evenly spaced values in (0, 1), as a TOML array: [0.01, 0.02, ...]
cut_list() {
  awk -v n="$1" 'BEGIN {
    printf "["
    for (i = 1; i <= n; i++) { printf "%s%g", sep, i / 100; sep = ", " }
    print "]"
  }'
}

begin "list given a non-array"
ga_cuts n_point 0.5
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] ga-1' [cut_points]: expected array of numbers"

begin "list with a string element"
ga_cuts n_point '[0.25, "0.5"]'
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] ga-1' [cut_points]: expected a number"

begin "list with a nan element"
ga_cuts n_point '[0.25, nan]'
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] ga-1' [cut_points]: nan is not a valid value"

begin "list of 65 elements"
ga_cuts uniform "$(cut_list 65)"
run -d -f case.toml
expect_error "Error at 'case.toml [[run]] ga-1' [cut_points]: too many elements (max 64)"

# Uniform crossover doesn't check cut points, so 64 of them load
begin "list of 64 elements"
ga_cuts uniform "$(cut_list 64)"
run -d -f case.toml
expect_dump cut_points "$(cut_list 64)"

finish
