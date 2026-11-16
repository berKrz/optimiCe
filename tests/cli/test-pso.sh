# PSO's own checks, from its validate. Each rejected value sits next to the accepted value
# just inside, read back from --dump-config
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

begin "swarm_size below 1"
one_run pso 'swarm_size = 0'
check
expect_error "Error at 'case.toml [[run]] pso-1': swarm_size must be positive"

for key in w w_end c1 c2; do
  begin "negative $key"
  one_run pso "$key = -0.1"
  check
  expect_error "Error at 'case.toml [[run]] pso-1': $key must be non-negative"
done

begin "negative v_max"
one_run pso 'v_max = -0.1'
check
expect_error "Error at 'case.toml [[run]] pso-1': v_max must be non-negative (0 disables the cap)"

# Particles take 3 x n_dims doubles each: at n_dims = 2, INT_MAX / 6 = 357913941 of them fit
begin "swarm too large for one buffer"
one_run pso 'swarm_size = 357913942'
check
expect_error "Error at 'case.toml [[run]] pso-1': swarm_size x n_dims is too large (at most 2147483647 elements per buffer)"

begin "largest swarm that fits"
one_run pso 'swarm_size = 357913941'
check
expect_dump swarm_size 357913941

finish
