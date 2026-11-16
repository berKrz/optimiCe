# GA's own checks, from its validate. Each rejected value sits next to the accepted value
# just inside, read back from --dump-config
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

begin "pop_size below 2"
one_run ga 'pop_size = 1'
check
expect_error "Error at 'case.toml [[run]] ga-1': pop_size must be >= 2"

begin "pop_size of 2"
one_run ga 'pop_size = 2'
check
expect_dump pop_size 2

for size in 1 64; do
  begin "ind_size of $size"
  one_run ga "ind_size = $size"
  check
  expect_error "Error at 'case.toml [[run]] ga-1': ind_size must be in [2, 63]"
done

for size in 2 63; do
  begin "ind_size of $size accepted"
  one_run ga "ind_size = $size"
  check
  expect_dump ind_size "$size"
done

begin "infinite domain"
one_run ga "$(printf 'domain_max = inf\ninit_max = 5.0')"
check
expect_error "Error at 'case.toml [[run]] ga-1': ga needs a finite domain: its bits decode onto [domain_min, domain_max]"

# At 22 bits each, INT_MAX / 22 = 97612893 dimensions fit
begin "too many bits per individual"
one_run ga 'n_dims = 97612894'
check
expect_error "Error at 'case.toml [[run]] ga-1': n_dims x ind_size is too large (at most 2147483647 elements per buffer)"

begin "most bits per individual"
one_run ga 'n_dims = 97612893'
check
expect_dump n_dims 97612893

# 64 dimensions of 63 bits take 63 words: INT_MAX / 63 = 34087042 individuals fit
begin "population too large for one buffer"
one_run ga "$(printf 'n_dims = 64\nind_size = 63\npop_size = 34087043')"
check
expect_error "Error at 'case.toml [[run]] ga-1': pop_size x n_dims x ind_size is too large (at most 2147483647 elements per buffer)"

begin "largest population that fits"
one_run ga "$(printf 'n_dims = 64\nind_size = 63\npop_size = 34087042')"
check
expect_dump pop_size 34087042

# 2 bits on [-5, 5]: levels -5, -5/3, 5/3, 5, none in [0, 1]
begin "init range holding no level"
one_run ga "$(printf 'ind_size = 2\ninit_min = 0.0\ninit_max = 1.0')"
check
expect_error "Error at 'case.toml [[run]] ga-1': the init range holds no value that ind_size bits can represent"

begin "mutation_rate of 1"
one_run ga 'mutation_rate = 1.0'
check
expect_error "Error at 'case.toml [[run]] ga-1': mutation_rate must be in [0.0, 1.0)"

begin "mutation_rate of 0"
one_run ga 'mutation_rate = 0.0'
check
expect_dump mutation_rate 0

begin "tournament_size below 2"
one_run ga "$(printf 'selection = "tournament"\ntournament_size = 1')"
check
expect_error "Error at 'case.toml [[run]] ga-1': tournament_size must be >= 2"

begin "tournament larger than the population"
one_run ga "$(printf 'selection = "tournament"\npop_size = 15\ntournament_size = 16')"
check
expect_error "Error at 'case.toml [[run]] ga-1': tournament_size must be at most pop_size (pop_size=15, tournament_size=16)"

begin "tournament of the whole population"
one_run ga "$(printf 'selection = "tournament"\npop_size = 15\ntournament_size = 15')"
check
expect_dump tournament_size 15

begin "tournament_size ignored under roulette"
one_run ga "$(printf 'selection = "roulette"\ntournament_size = 1')"
check
expect_dump tournament_size 1

begin "no cut points"
one_run ga 'cut_points = []'
check
expect_error "Error at 'case.toml [[run]] ga-1': cut_points must have at least one element"

begin "cut point at 0"
one_run ga 'cut_points = [0.0]'
check
expect_error "Error at 'case.toml [[run]] ga-1': cut_points[0] = 0 must be in (0.0, 1.0)"

begin "cut point at 1"
one_run ga 'cut_points = [1.0]'
check
expect_error "Error at 'case.toml [[run]] ga-1': cut_points[0] = 1 must be in (0.0, 1.0)"

begin "cut points out of order"
one_run ga 'cut_points = [0.5, 0.25]'
check
expect_error "Error at 'case.toml [[run]] ga-1': cut_points must be strictly increasing (0.25 after 0.5)"

begin "cut point on bit 0"
one_run ga 'cut_points = [0.01]'
check
expect_error "Error at 'case.toml [[run]] ga-1': cut_points[0] = 0.01 lands on bit 0 with ind_size = 22 (needs >= 0.0454545)"

begin "two cut points on the same bit"
one_run ga 'cut_points = [0.5, 0.51]'
check
expect_error "Error at 'case.toml [[run]] ga-1': cut_points 0.5 and 0.51 land on the same bit (11) with ind_size = 22"

begin "cut points ignored under uniform crossover"
one_run ga "$(printf 'crossover = "uniform"\ncut_points = [0.0]')"
check
expect_dump cut_points "[0]"

for pressure in 0.9 2.1; do
  begin "rank_pressure of $pressure"
  one_run ga "$(printf 'selection = "rank"\nrank_pressure = %s' "$pressure")"
  check
  expect_error "Error at 'case.toml [[run]] ga-1': rank_pressure must be in [1.0, 2.0]"
done

begin "rank_pressure ignored outside rank selection"
one_run ga "$(printf 'selection = "roulette"\nrank_pressure = 0.9')"
check
expect_dump rank_pressure 0.9

finish
