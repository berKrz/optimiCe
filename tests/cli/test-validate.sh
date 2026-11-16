# Values every algorithm shares: the runner's checks, the fitness function's limits and the bounds
# rule. Each algorithm's own checks are in test-<algorithm>.sh. Each rejected value sits next to
# the accepted value just inside, read back from --dump-config
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

# --- Runner fields ---

begin "n_dims below 1"
one_run pso 'n_dims = 0'
check
expect_error "Error at 'case.toml [[run]] pso-1': n_dims must be at least 1"

begin "n_dims of 1"
one_run pso 'n_dims = 1'
check
expect_dump n_dims 1

begin "empty domain"
one_run pso "$(printf 'domain_min = 1.0\ndomain_max = 1.0')"
check
expect_error "Error at 'case.toml [[run]] pso-1': domain_min must be strictly less than domain_max"

begin "domain too wide"
one_run pso "$(printf 'domain_min = -1e308\ndomain_max = 1e308')"
check
expect_error "Error at 'case.toml [[run]] pso-1': the domain is too wide: domain_max - domain_min must be finite"

begin "infinite domain_min without init_min"
one_run pso 'domain_min = -inf'
check
expect_error "Error at 'case.toml [[run]] pso-1': domain_min is infinite, so init_min must be set"

begin "infinite domain_max without init_max"
one_run pso 'domain_max = inf'
check
expect_error "Error at 'case.toml [[run]] pso-1': domain_max is infinite, so init_max must be set"

begin "infinite init end"
one_run pso "$(printf 'domain_min = -inf\ninit_min = -inf')"
check
expect_error "Error at 'case.toml [[run]] pso-1': init_min and init_max must be finite"

begin "empty init range"
one_run pso "$(printf 'init_min = 1.0\ninit_max = 1.0')"
check
expect_error "Error at 'case.toml [[run]] pso-1': init_min must be strictly less than init_max"

begin "init range too wide"
one_run pso "$(printf 'domain_min = -inf\ndomain_max = inf\ninit_min = -1e308\ninit_max = 1e308')"
check
expect_error "Error at 'case.toml [[run]] pso-1': the init range is too wide: init_max - init_min must be finite"

begin "init range outside the domain"
one_run pso "$(printf 'init_min = -6.0')"
check
expect_error "Error at 'case.toml [[run]] pso-1': the init range must be inside the domain"

begin "init range equal to the domain"
one_run pso "$(printf 'init_min = -5.0\ninit_max = 5.0')"
check
expect_dump init_min -5

begin "iterations below 1"
one_run pso 'iterations = 0'
check
expect_error "Error at 'case.toml [[run]] pso-1': iterations must be positive"

begin "iterations of 1"
one_run pso 'iterations = 1'
check
expect_dump iterations 1

begin "negative stagnation"
one_run pso 'stagnation = -1'
check
expect_error "Error at 'case.toml [[run]] pso-1': stagnation must be non-negative (0 disables it)"

begin "stagnation of 0"
one_run pso 'stagnation = 0'
check
expect_dump stagnation 0

# --- The fitness function's limits ---

begin "rosenbrock with 1 dimension"
one_run pso "$(printf 'fitness = "rosenbrock"\nn_dims = 1')"
check
expect_error "Error at 'case.toml [[run]] pso-1': for rosenbrock, n_dims must be at least 2 (current: 1)"

begin "rosenbrock with 2 dimensions"
one_run pso "$(printf 'fitness = "rosenbrock"\nn_dims = 2')"
check
expect_dump n_dims 2

begin "eggholder with 3 dimensions"
one_run pso "$(printf 'fitness = "eggholder"\nn_dims = 3')"
check
expect_error "Error at 'case.toml [[run]] pso-1': for eggholder, n_dims must be at most 2 (current: 3)"

begin "rastrigin domain past its limits"
one_run pso "$(printf 'fitness = "rastrigin"\ndomain_min = -5.13\ndomain_max = 5.12')"
check
expect_error "Error at 'case.toml [[run]] pso-1': for rastrigin, the domain must be contained in [-5.12, 5.12]"

begin "rastrigin domain at its limits"
one_run pso "$(printf 'fitness = "rastrigin"\ndomain_min = -5.12\ndomain_max = 5.12')"
check
expect_dump domain_min -5.12

# --- Bounds ---

begin "random bounds on a half-bounded domain"
one_run pso "$(printf 'bounds = "random"\ndomain_min = 0.0\ndomain_max = inf\ninit_max = 5.0')"
check
expect_error "Error at 'case.toml [[run]] pso-1': bounds = random needs a domain that is finite, or unbounded on both sides: there is no uniform draw from a half-bounded one"

begin "random bounds on an unbounded domain"
one_run pso "$(printf 'bounds = "random"\ndomain_min = -inf\ndomain_max = inf\ninit_min = -5.0\ninit_max = 5.0')"
check
expect_dump bounds random

finish
