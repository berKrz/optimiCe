# Shared by the test-*.sh scripts: each case runs optimice in a fresh temporary directory, on a
# config written there as case.toml, and prints "ok N - name" or "not ok N - name" (TAP)
# Run through `make test-cli`, which sets OPTIMICE to the binary under test

: "${OPTIMICE:?run through make test-cli}"

n=0
failed=0
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

# Starts a case named $1, in a fresh directory that holds only what the case and optimice write
# there. The harness keeps its own files apart: the run's stdout in $out and stderr in $err, and
# anything else a check needs in $tmp
begin() {
  n=$((n + 1))
  name=$1
  out=$root/$n.stdout
  err=$root/$n.stderr
  tmp=$root/$n.tmp
  mkdir "$root/$n" "$tmp" && cd "$root/$n" || exit 1
}

# Writes stdin as the case's config, case.toml
config() { cat > case.toml; }

# Runs optimice with the given arguments; keeps stdout in $out, stderr in $err, the status in
# $status. stdin is empty, so nothing ever waits for a key press
run() {
  "$OPTIMICE" "$@" > "$out" 2> "$err" < /dev/null
  status=$?
}

pass() { echo "ok $n - $name"; }

# Fails the case; stdin explains why, printed as TAP diagnostics
fail() {
  echo "not ok $n - $name"
  sed 's/^/#   /'
  failed=$((failed + 1))
}

# Checks the last run failed with the error $1, followed by the --help hint unless $2 is "nohint",
# and printed nothing on stdout
expect_error() {
  { printf '%s\n' "$1"; [ "$2" = nohint ] || echo "Run with --help to see available options."; } \
    > "$tmp/expected"
  if [ "$status" -ne 1 ]; then
    { echo "exit status $status, expected 1"; cat "$err"; } | fail
  elif [ -s "$out" ]; then
    { echo "unexpected output on stdout:"; cat "$out"; } | fail
  elif ! diff "$tmp/expected" "$err" > "$tmp/diff"; then
    fail < "$tmp/diff"
  else
    pass
  fi
}

# The same for an error whose message ends with text from elsewhere (tomlc17, the system): only
# its first line's beginning, our part, is checked: $1
expect_error_start() {
  if [ "$status" -ne 1 ]; then
    { echo "exit status $status, expected 1"; cat "$err"; } | fail
  elif [ -s "$out" ]; then
    { echo "unexpected output on stdout:"; cat "$out"; } | fail
  else
    case $(head -n 1 "$err") in
      "$1"*) pass ;;
      *) { echo "expected stderr to start with: $1"; echo "got:"; cat "$err"; } | fail ;;
    esac
  fi
}

# Checks the last run succeeded
expect_ok() {
  if [ "$status" -ne 0 ]; then
    { echo "exit status $status, expected 0"; cat "$err"; } | fail
  else
    pass
  fi
}

# Checks the last run succeeded and that --dump-config showed field $1 as $2, in the first section
# that has it
expect_dump() {
  got=$(sed -n "s/^$1  *= //p" "$out" | head -n 1)
  if [ "$status" -ne 0 ]; then
    { echo "exit status $status, expected 0"; cat "$err"; } | fail
  elif [ "$got" != "$2" ]; then
    echo "$1: expected '$2', got '$got'" | fail
  else
    pass
  fi
}

# Ends the script: the TAP plan, then a failing status if any case failed
finish() {
  echo "1..$n"
  [ "$failed" -eq 0 ]
}
