# Successful runs: results independent of threads, the order of what's printed, the single-job
# progress lines, and unbounded searches. Numbers are compared between runs, never written here
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

# --- Threads ---

# A run's result doesn't depend on the batch around it: each of the 6 jobs below, run on the
# thread pool, gives the same CSV values as the same job run alone (seed 100 + its replicate)
begin "a batch on threads matches each job run alone"
cat > "$tmp/runs" <<'TOML'
[[run]]
algorithm = "pso"
n_dims = 3
[[run]]
algorithm = "sma"
TOML
{ printf '[common]\nseed = 100\nreplicates = 3\niterations = 30\n'; cat "$tmp/runs"; } | config
run -f case.toml -o batch.csv
batch_status=$status
problems=
k=0
for label in pso-1 sma-1; do
  k=$((k + 1))
  for r in 0 1 2; do
    # Run k alone: the file's k-th [[run]], with its own [common]
    {
      printf '[common]\nseed = %d\niterations = 30\n' $((100 + r))
      awk -v k="$k" '/^\[\[run\]\]/ { n++ } n == k' "$tmp/runs"
    } > alone.toml
    "$OPTIMICE" -f alone.toml -o alone.csv > /dev/null 2>&1 < /dev/null || problems="$problems $label/r$r:failed"
    # Columns 4 on: fitness, iterations, evaluations, stop, position
    want=$(grep "^$label,$r," batch.csv | cut -d, -f4-)
    got=$(tail -n 1 alone.csv | cut -d, -f4-)
    [ -n "$want" ] && [ "$want" = "$got" ] || problems="$problems $label/r$r"
  done
done
if [ "$batch_status" -ne 0 ]; then
  { echo "exit status $batch_status, expected 0"; cat "$err"; } | fail
elif [ -n "$problems" ]; then
  echo "differs from the job run alone:$problems" | fail
else
  pass
fi

# --- What's printed ---

begin "job lines in job order"
config <<'TOML'
[common]
replicates = 2
iterations = 5
[[run]]
algorithm = "sma"
[[run]]
algorithm = "pso"
TOML
run -f case.toml
printf '%s\n' "sma-1 r0" "sma-1 r1" "pso-1 r0" "pso-1 r1" > "$tmp/expected"
awk '$2 ~ /^r[0-9]+$/ { print $1, $2 }' "$out" > "$tmp/got"
if [ "$status" -ne 0 ]; then
  { echo "exit status $status, expected 0"; cat "$err"; } | fail
elif ! diff "$tmp/expected" "$tmp/got" > "$tmp/diff"; then
  fail < "$tmp/diff"
else
  pass
fi

# One progress line per iteration done, numbered from 0, then the footer's count agrees; once
# with the whole budget, once stopped early
progress_case() {
  run -f case.toml
  done_count=$(sed -n 's/^Iterations: //p' "$out")
  lines=$(grep -c '^Iteration ' "$out")
  last=$(grep '^Iteration ' "$out" | tail -n 1 | awk '{ print $2 }')
  if [ "$status" -ne 0 ]; then
    { echo "exit status $status, expected 0"; cat "$err"; } | fail
  elif [ "$done_count" != "$1" ] || [ "$lines" != "$1" ] || [ "$last" != $(($1 - 1)) ]; then
    echo "expected $1 iterations; footer says '$done_count', $lines progress lines, last numbered '$last'" | fail
  else
    pass
  fi
}

begin "single job: a progress line per iteration"
config <<'TOML'
[common]
iterations = 7
[[run]]
algorithm = "sma"
TOML
progress_case 7

# Seed 5 stalls after 6 generations of ga with stagnation = 2
begin "single job stopped early: a progress line per iteration done"
config <<'TOML'
[common]
seed = 5
iterations = 50
stagnation = 2
[[run]]
algorithm = "ga"
TOML
progress_case 6

# --- --dump-config ---

begin "generated seed marked as such"
config <<'TOML'
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
if [ "$status" -ne 0 ]; then
  { echo "exit status $status, expected 0"; cat "$err"; } | fail
elif ! grep -Eq '^seed += 0x[0-9a-f]{8}  \(generated\)$' "$out"; then
  { echo "no generated seed line:"; grep '^seed' "$out"; } | fail
else
  pass
fi

begin "given seed not marked"
config <<'TOML'
[common]
seed = 42
[[run]]
algorithm = "pso"
TOML
run -d -f case.toml
expect_dump seed 0x0000002a

# --- Unbounded searches ---

# The whole real line, starting from [-5, 5]: every number in the result is finite
for algo in pso sma; do
  begin "$algo on an unbounded domain"
  printf '[common]\nn_dims = 3\niterations = 50\ndomain_min = -inf\ndomain_max = inf\ninit_min = -5.0\ninit_max = 5.0\n[[run]]\nalgorithm = "%s"\n' "$algo" | config
  run -f case.toml -o out.csv
  if [ "$status" -ne 0 ]; then
    { echo "exit status $status, expected 0"; cat "$err"; } | fail
  elif tail -n 1 out.csv | cut -d, -f4,8 | grep -Eqi 'inf|nan'; then
    { echo "non-finite result:"; tail -n 1 out.csv; } | fail
  else
    pass
  fi
done

finish
