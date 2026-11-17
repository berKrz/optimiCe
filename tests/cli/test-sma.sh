# SMA's own checks, from its validate. Each rejected value sits next to the accepted value
# just inside, read back from --dump-config
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

begin "pop_size below 1"
one_run sma 'pop_size = 0'
check
expect_error "Error at 'case.toml [[run]] sma-1': pop_size must be positive"

for z in -0.1 1.1; do
  begin "z of $z"
  one_run sma "z = $z"
  check
  expect_error "Error at 'case.toml [[run]] sma-1': z must be in [0.0, 1.0]"
done

for z in 0 1; do
  begin "z of $z accepted"
  one_run sma "z = $z.0"
  check
  expect_dump z "$z"
done

# Slimes take 2 x n_dims + 1 doubles each: at n_dims = 2, INT_MAX / 5 = 429496729 of them fit
begin "population too large for one buffer"
one_run sma 'pop_size = 429496730'
check
expect_error "Error at 'case.toml [[run]] sma-1': pop_size x n_dims is too large (at most 2147483647 elements per buffer)"

begin "largest population that fits"
one_run sma 'pop_size = 429496729'
check
expect_dump pop_size 429496729

finish
