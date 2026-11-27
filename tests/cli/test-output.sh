# -o / --output: where the CSV goes, its errors, and what it holds
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

# A short single job
one_job() {
  config <<'TOML'
[common]
iterations = 5
[[run]]
algorithm = "pso"
TOML
}

# Checks the last run succeeded, wrote exactly one generated name, case-YYYYMMDD-HHMMSS.csv, in
# directory $1 (. for the current one), and said so on stderr
expect_generated() {
  names=
  for f in "$1"/case-*.csv; do
    base=${f##*/}
    if echo "$base" | grep -Eq '^case-[0-9]{8}-[0-9]{6}\.csv$'; then names="$names$base "; fi
  done
  names=${names% }
  if [ "$status" -ne 0 ]; then
    { echo "exit status $status, expected 0"; cat "$err"; } | fail
  elif [ -z "$names" ] || [ "$names" != "${names% *}" ]; then
    { echo "expected one generated file in $1, found:"; ls -A "$1"; } | fail
  else
    path=$names
    [ "$1" = . ] || path="$1/$names"
    if [ "$(cat "$err")" != "Output: $path" ]; then
      { echo "expected stderr: Output: $path"; echo "got:"; cat "$err"; } | fail
    else
      pass
    fi
  fi
}

# Checks the last run succeeded and wrote the regular file $1, a CSV
expect_file() {
  if [ "$status" -ne 0 ]; then
    { echo "exit status $status, expected 0"; cat "$err"; } | fail
  elif [ ! -f "$1" ]; then
    echo "no file $1" | fail
  elif [ "$(head -n 1 "$1" | cut -c1-11)" != "# optimice " ]; then
    { echo "$1 isn't a CSV from optimice:"; head -n 3 "$1"; } | fail
  else
    pass
  fi
}

# --- Where the file goes ---

begin "bare -o: generated name here"
one_job
run -f case.toml -o
expect_generated .

begin "-o dir/: created, generated name inside"
one_job
run -f case.toml -o out/
expect_generated out

begin "-o a/b/: both levels created"
one_job
run -f case.toml -o a/b/
expect_generated a/b

begin "-o dir without a slash, when dir exists"
one_job
mkdir out
run -f case.toml -o out
expect_generated out

begin "-o name, when no such directory exists: the file itself"
one_job
run -f case.toml -o out
expect_file out

begin "-o a/b/c.csv: missing directories created"
one_job
run -f case.toml -o a/b/c.csv
expect_file a/b/c.csv

begin "-o on an existing file overwrites it"
one_job
echo "old" > out.csv
run -f case.toml -o out.csv
expect_file out.csv

# --- Errors ---

begin "-o with an empty path"
one_job
run -f case.toml -o ""
expect_error "Error: --output was given an empty path"

# The message ends with the system's own text; no job runs, so stdout stays empty
begin "-o where a directory can't be"
one_job
run -f case.toml -o case.toml/x
expect_error_start "Error: cannot write 'case.toml/x': "

# --- What the file holds ---

# 2 runs x 3 replicates, base seed 100
begin "header and rows"
config <<'TOML'
[common]
seed = 100
replicates = 3
iterations = 5
[[run]]
algorithm = "pso"
[[run]]
algorithm = "ga"
TOML
run -f case.toml -o out.csv
{
  # Read from stdin, cksum prints just "<crc> <length>"
  echo "# config: case.toml (cksum $(cksum < case.toml))"
  echo "# jobs: 6"
  echo "run,replicate,seed,fitness,iterations,evaluations,stop,position"
  printf '%s\n' "pso-1,0,0x00000064" "pso-1,1,0x00000065" "pso-1,2,0x00000066" \
    "ga-1,0,0x00000064" "ga-1,1,0x00000065" "ga-1,2,0x00000066"
} > "$tmp/expected"
# The version and date lines vary; each row is compared up to its seed
{
  sed -n '2p;4p;5p' out.csv
  tail -n +6 out.csv | cut -d, -f1-3
} > "$tmp/got"
if [ "$status" -ne 0 ]; then
  { echo "exit status $status, expected 0"; cat "$err"; } | fail
elif [ "$(head -n 1 out.csv | cut -c1-11)" != "# optimice " ] ||
  ! sed -n 3p out.csv | grep -Eq '^# date: [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:]{8} [+-][0-9]{4}$'; then
  { echo "unexpected version or date line:"; head -n 3 out.csv; } | fail
elif ! diff "$tmp/expected" "$tmp/got" > "$tmp/diff"; then
  fail < "$tmp/diff"
else
  pass
fi

# A run that does all its iterations stops on the cap
begin "stop of a full run"
config <<'TOML'
[common]
iterations = 5
[[run]]
algorithm = "pso"
TOML
run -f case.toml -o out.csv
got=$(tail -n +6 out.csv | cut -d, -f5,7)
if [ "$status" -ne 0 ]; then
  { echo "exit status $status, expected 0"; cat "$err"; } | fail
elif [ "$got" != "5,iterations" ]; then
  echo "iterations and stop: got '$got', expected '5,iterations'" | fail
else
  pass
fi

finish
