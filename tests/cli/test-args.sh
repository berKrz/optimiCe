# The command line: options, their errors, --help and --list
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

# --- Errors ---

begin "unknown long option"
run --nope
expect_error "Error: invalid option: --nope"

begin "unknown short option"
run -x
expect_error "Error: invalid option: -x"

begin "unknown short option inside a cluster"
run -ix
expect_error "Error: invalid option: -x"

begin "-f without its argument"
run -f
expect_error "Error: missing argument for option: -f"

begin "--config without its argument"
run --config
expect_error "Error: missing argument for option: --config"

begin "stray argument"
one_run pso
run -f case.toml extra
expect_error "Error: unexpected argument"

begin "no config"
run -d
expect_error "Error: --config / -f is required"

# --- Short options in either case ---

begin "-F works like -f"
one_run pso
run -F case.toml -D
expect_ok

# --- --interactive ---

begin "--interactive with two runs"
config <<'TOML'
[common]
iterations = 5
[[run]]
algorithm = "pso"
[[run]]
algorithm = "ga"
TOML
run -i -f case.toml
expect_error "Error: --interactive needs a single job (got 2 runs x 1 replicate = 2)"

begin "--interactive with two replicates"
config <<'TOML'
[common]
iterations = 5
replicates = 2
[[run]]
algorithm = "pso"
TOML
run -i -f case.toml
expect_error "Error: --interactive needs a single job (got 1 run x 2 replicates = 2)"

# Checked before --dump-config, which then runs nothing
begin "--interactive with one job"
one_run pso
run -i -d -f case.toml
expect_ok

# --- --help and --list ---

begin "--help"
run --help
expect_ok

begin "--help for an algorithm"
run --help pso
expect_ok

begin "--help for an unknown algorithm"
run --help nope
expect_error "Error: unknown algorithm: 'nope'"

begin "--list"
run --list
cat > "$tmp/expected" <<'TEXT'
Algorithms:
  ga
  pso
  sma

Fitness functions:
  sphere        n_dims: any         domain: any
  rosenbrock    n_dims: 2 or more   domain: any
  rastrigin     n_dims: any         domain: within [-5.12, 5.12]
  griewank      n_dims: any         domain: within [-600, 600]
  ackley        n_dims: any         domain: within [-5, 5]
  eggholder     n_dims: 2           domain: within [-512, 512]
  schwefel      n_dims: any         domain: within [-500, 500]
  schaffer_f6   n_dims: 2           domain: within [-100, 100]
TEXT
if [ "$status" -ne 0 ]; then
  echo "exit status $status, expected 0" | fail
elif ! diff "$tmp/expected" "$out" > "$tmp/diff"; then
  fail < "$tmp/diff"
else
  pass
fi

finish
