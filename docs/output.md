# Output

optimiCe prints its results to the terminal, and with `-o` also writes them to a CSV file. What the terminal shows depends on the batch's size: a single job prints its progress and a full result, a batch of several jobs prints one line per job.

Results go to stdout. Anything that depends on the machine or the moment, such as the number of threads and the results file's path, goes to stderr, so redirected output is the same on every machine.

## A single job

A batch of one job, one `[[run]]` with `replicates = 1`, prints a line per iteration with the algorithm's current best, then the final result:

```
Iteration   0   best = 0.426492
Iteration   1   best = 0.096761
...
Iteration  99   best = 7.87217e-11
==================================================
=== Final Result =================================

Best solution:
   pos: [  -0.000  -0.000 ]
   fitness = 7.872174776306589e-11

Iterations: 100
Evaluations: 3030
Algorithm: pso    Seed: 0x0000002a
==================================================
```

When the run stops early, the result says why, e.g. `Stopped early: no improvement for 200 iterations`. The seed is the one the job ran with: setting `seed` to it reproduces the run, which is what makes a generated seed useful.

## A batch

A batch of several jobs prints a header, then one line per job:

```
Batch: 2 runs x 3 replicates = 6 jobs    Base seed: 0x0000002a

pso-1  r0  seed 0x0000002a  best = 1.989918115088523         iters 1000  evals 30030
pso-1  r1  seed 0x0000002b  best = 4.974867135774531         iters 1000  evals 30030
pso-1  r2  seed 0x0000002c  best = 44.85104794059824         iters 369   evals 11100       stagnation
ga-1   r0  seed 0x0000002a  best = 57.63045559403642         iters 413   evals 16560       stagnation
ga-1   r1  seed 0x0000002b  best = 39.94383383288411         iters 490   evals 19640       stagnation
ga-1   r2  seed 0x0000002c  best = 37.584355934977964        iters 499   evals 20000       stagnation
```

Each line holds the run's label, the replicate, the job's seed, the best fitness, the iterations completed, the fitness evaluations used, and the early stop, `target` or `stagnation`, when there was one.

Jobs run in parallel on as many threads as there are CPU cores, up to the number of jobs; the thread count goes to stderr. Lines come out in job order, every replicate of the first run, then the second, and so on, each as soon as it and every job before it have finished. A job's result depends only on its seed, so the output is the same whatever the number of threads.

## Numbers

Final results, the single job's fitness and each job line's best, are printed in the shortest form that reads back as exactly the same value, so a printed result identifies the double the run produced. Per-iteration lines and positions are rounded for reading: 6 significant digits, and 3 decimals for positions.

## Results file

`-o` writes the results to a CSV file as well as printing them; the terminal output doesn't change. Where the file goes depends on the path:

- `-o` with no path: a generated name in the current directory, `<config name>-<YYYYMMDD-HHMMSS>.csv`, e.g. `example-20261109-143205.csv`.
- `-o results/`, ending in a slash, or naming a directory that already exists: a generated name inside it.
- `-o results/run.csv`: that file, replaced if it already exists.

Missing directories are created. A generated name never replaces an existing file: a second batch from the same config within the same second gets `-2`, `-3`, … appended. The chosen path is printed to stderr (`Output: …`).

The file starts with `#` comment lines saying where the results came from:

```
# optimice 85d62f0-dirty
# config: experiments/rastrigin.toml (cksum 114835441 2397)
# date: 2026-11-09 14:32:05 -0300
# jobs: 12
```

The first line is the commit the binary was built from, with `-dirty` when the tree had uncommitted changes. The second is the config file as given on the command line, with its `cksum`: running `cksum experiments/rastrigin.toml` prints the same two numbers as long as the file hasn't changed since. The date is local time, with its UTC offset.

After the header comes the table, one row per job, in the same order as the terminal lines:

```
run,replicate,seed,fitness,iterations,evaluations,stop,position
pso-1,0,0x0000002a,1.989918115088523,1000,30030,none,0.9949595703309596 -1.9433042650135142e-08 ...
pso-1,1,0x0000002b,4.974867135774531,1000,30030,none,9.029679007347454e-05 -1.9299212039898897e-05 ...
```

| Column        | Meaning                                                                          |
|---------------|----------------------------------------------------------------------------------|
| `run`         | The run's label, as in the terminal output                                       |
| `replicate`   | Replicate number, from 0                                                         |
| `seed`        | The job's seed, in hex; it can be pasted back into `seed =` to reproduce the job |
| `fitness`     | Best fitness found                                                               |
| `iterations`  | Iterations completed: fewer than the budget after an early stop                  |
| `evaluations` | Fitness evaluations used                                                         |
| `stop`        | `none`, `target` or `stagnation`                                                 |
| `position`    | The best position, its coordinates separated by spaces                           |

Every number is written in the shortest form that reads back exactly, positions included, so the file holds the same doubles the run produced. The file is plain CSV, with `.` as the decimal separator and `,` between fields. Most CSV readers skip the header given a comment character, e.g. `pandas.read_csv(path, comment="#")` in Python or `read.csv(path, comment.char = "#")` in R.

Rows are written as jobs finish, in job order, and each one is saved immediately: a batch that's interrupted leaves every row it completed, and comparing the number of rows with `# jobs:` tells a complete file from an interrupted one. Apart from the date, the file is the same whatever the number of threads.

## Inspecting a configuration

`--dump-config` (`-d`) prints the batch's resolved configuration and exits without running anything: the base seed and `replicates` once, then every run's full configuration, defaults included, as the run will see it. It never creates or touches a results file, even with `-o`.

```
# batch
seed           = 0x0000002a
replicates     = 3

# run pso-1
direction      = minimize
n_dims         = 10
...

# pso fields
swarm_size   = 30
w            = 0.9
...
```

## Interactive mode

`--interactive` (`-i`) steps through a run one iteration at a time: after each iteration, the algorithm prints its population and waits for Enter. What it shows is the algorithm's own, usually every individual's position and fitness, with a legend under each view explaining its markers. The final result is printed as usual at the end.

It needs a single job, one `[[run]]` and `replicates = 1`, and is rejected otherwise.

## Failures during a batch

Every configuration check runs before the first job, so a batch that starts normally runs to the end. The exceptions are failures the configuration can't predict, such as running out of memory or a results file that can't be written: those stop the whole batch with an error, which names the job when it happened inside one, e.g. `Error at 'pso-2, replicate 7': memory allocation failed`. Lines already printed stay printed, and rows already written stay in the file.
