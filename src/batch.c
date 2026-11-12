#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "algorithms.h"
#include "common.h"

// Runs every job of a loaded Batch and prints its results
// Job j is run j / replicates, replicate j % replicates; results land in results[j], and lines
// are printed in job order (see job_finished), so the output doesn't depend on finishing order
// Jobs run in parallel on min(jobs, CPU cores) threads. A job only touches its own thread's state
// (g_cfg, the algorithm's cfg, the RNG, g_where) and its own results[j]; the Batch is read-only

int batch_jobs(const Batch* batch) { return batch->n_runs * batch->replicates; }

void load_run(const RunConfig* rc) {
  const Algorithm* algo = rc->cfg.algorithm;
  g_cfg                 = rc->cfg;
  memcpy(algo->cfg_base(), rc->algo_cfg, algo->cfg_size);
}

static uint32_t job_seed(const Batch* batch, int r) { return batch->seed + (uint32_t) r; }

static void run_job(const Batch* batch, int j, RunResult* out) {
  const RunConfig* rc = &batch->runs[j / batch->replicates];
  int              r  = j % batch->replicates;

  char where[64];
  snprintf(where, sizeof(where), "%s, replicate %d", rc->label, r);

  load_run(rc);
  g_cfg.seed = job_seed(batch, r);
  g_where    = where;
  rng_init(g_cfg.seed);
  evaluations_reset();
  rc->cfg.algorithm->run(out);
  out->evaluations = evaluations();
  g_where          = NULL;
}

// --- Multi-job output ---

typedef struct {
  const Batch*     batch;
  const RunResult* results;
  char*            done;    // done[j] = 1 once job j has finished
  int              cursor;  // next job to print
  int              label_w;
  int              rep_w;
  int              iter_w;  // digits of the largest iteration budget in the batch
} Printer;

static int digits(int n) {
  int d = 1;
  while (n >= 10) n /= 10, d++;
  return d;
}

static Printer printer_init(const Batch* batch, const RunResult* results, char* done) {
  Printer p = {
      .batch = batch, .results = results, .done = done, .rep_w = digits(batch->replicates - 1)};
  for (int k = 0; k < batch->n_runs; k++) {
    int w = (int) strlen(batch->runs[k].label);
    if (w > p.label_w) p.label_w = w;
    w = digits(batch->runs[k].cfg.iterations);
    if (w > p.iter_w) p.iter_w = w;
  }
  return p;
}

static void print_job_line(const Printer* p, int j) {
  const RunConfig* rc  = &p->batch->runs[j / p->batch->replicates];
  const RunResult* res = &p->results[j];
  int              r   = j % p->batch->replicates;

  char fit[FMT_EXACT_LEN], best[FMT_EXACT_LEN + 8], evals[32];
  fmt_exact(fit, res->fitness);
  snprintf(best, sizeof(best), "best = %s", fit);
  snprintf(evals, sizeof(evals), "evals %" PRIu64, res->evaluations);

  // best is padded to its longest form, "best = " and 24 characters: -1.2345678901234567e-308
  printf("%-*s  r%-*d  seed 0x%08x  %-31s  iters %-*d  ", p->label_w, rc->label, p->rep_w, r,
         job_seed(p->batch, r), best, p->iter_w, res->stop.iterations);
  switch (res->stop.reason) {
    case STOP_NONE:       printf("%s\n", evals); break;
    case STOP_TARGET:     printf("%-16s  target\n", evals); break;
    case STOP_STAGNATION: printf("%-16s  stagnation\n", evals); break;
  }
}

// The job's row in the --output file, if any; called right after its terminal line, so both come
// out in job order
static void write_job_row(const Batch* batch, int j, const RunResult* res) {
  const RunConfig* rc = &batch->runs[j / batch->replicates];
  int              r  = j % batch->replicates;
  output_row(rc->label, r, job_seed(batch, r), rc->cfg.n_dims, res);
}

// Marks job j finished and prints every line the cursor can now reach: a line is printed once all
// jobs before it have finished, so lines come out in job order as soon as they can
static void job_finished(Printer* p, int j) {
  int total  = batch_jobs(p->batch);
  p->done[j] = 1;
  while (p->cursor < total && p->done[p->cursor]) {
    print_job_line(p, p->cursor);
    write_job_row(p->batch, p->cursor, &p->results[p->cursor]);
    p->cursor++;
  }
  fflush(stdout);
}

// --- Worker pool ---
//
// Every job is known up front, so the pool is a shared counter: each worker takes the next job
// number, runs it, and takes another until none remain, then exits
// Workers never wait for each other; the mutex only guards the printer

typedef struct {
  const Batch* batch;
  RunResult*   results;
  Printer*     printer;
  atomic_int   next;  // next job to hand out
  mtx_t        print_lock;
} Pool;

static int worker(void* arg) {
  Pool* pool  = arg;
  int   total = batch_jobs(pool->batch);

  for (;;) {
    int j = atomic_fetch_add(&pool->next, 1);
    if (j >= total) break;

    run_job(pool->batch, j, &pool->results[j]);

    mtx_lock(&pool->print_lock);
    job_finished(pool->printer, j);
    mtx_unlock(&pool->print_lock);
  }
  return 0;
}

// More threads than cores only adds switching overhead for CPU-bound jobs
static int thread_count(int total) {
  long cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores < 1) cores = 1;
  return total < cores ? total : (int) cores;
}

static void run_pool(Pool* pool, int n_threads) {
  atomic_init(&pool->next, 0);
  if (mtx_init(&pool->print_lock, mtx_plain) != thrd_success) die("could not create a mutex");

  if (n_threads == 1) {
    worker(pool);  // nothing to share the work with: run it on the calling thread
  } else {
    thrd_t* threads = xmalloc((size_t) n_threads * sizeof(thrd_t));
    for (int i = 0; i < n_threads; i++)
      if (thrd_create(&threads[i], worker, pool) != thrd_success)
        die("could not start a worker thread");
    for (int i = 0; i < n_threads; i++) thrd_join(threads[i], NULL);
    free(threads);
  }

  mtx_destroy(&pool->print_lock);
}

// --- Entry point ---

void run_batch(const Batch* batch) {
  int total = batch_jobs(batch);

  // One pos buffer for every job, each slice sized by its run's n_dims
  size_t n_pos = 0;
  for (int k = 0; k < batch->n_runs; k++)
    n_pos += (size_t) batch->runs[k].cfg.n_dims * batch->replicates;

  RunResult* results = xcalloc((size_t) total, sizeof(RunResult));
  double*    pos     = xmalloc(n_pos * sizeof(double));
  char*      done    = xcalloc((size_t) total, 1);

  size_t off = 0;
  for (int j = 0; j < total; j++) {
    results[j].pos  = pos + off;
    off            += (size_t) batch->runs[j / batch->replicates].cfg.n_dims;
  }

  if (total == 1) {
    // Single job: per-iteration output, then the full footer (g_cfg still holds the job's config)
    run_job(batch, 0, &results[0]);
    print_result(&results[0]);
    write_job_row(batch, 0, &results[0]);
  } else {
    int n_threads = thread_count(total);

    printf("Batch: %d run%s x %d replicate%s = %d jobs    Base seed: 0x%08x\n", batch->n_runs,
           batch->n_runs == 1 ? "" : "s", batch->replicates, batch->replicates == 1 ? "" : "s",
           total, batch->seed);
    // The thread count depends on the machine, so it stays out of stdout
    fflush(stdout);
    fprintf(stderr, "Threads: %d\n", n_threads);
    printf("\n");

    Printer printer = printer_init(batch, results, done);
    Pool    pool    = {.batch = batch, .results = results, .printer = &printer};
    run_pool(&pool, n_threads);
  }

  free(done);
  free(pos);
  free(results);
}

void dump_batch(const Batch* batch) {
  printf("# batch\n");
  printf("seed           = 0x%08x%s\n", batch->seed, batch->seed_generated ? "  (generated)" : "");
  printf("replicates     = %d\n", batch->replicates);

  for (int k = 0; k < batch->n_runs; k++) {
    printf("\n# run %s\n", batch->runs[k].label);
    load_run(&batch->runs[k]);
    dump_config();
  }
}

void free_batch(Batch* batch) {
  for (int k = 0; k < batch->n_runs; k++) free(batch->runs[k].algo_cfg);
  free(batch->runs);
  batch->runs   = NULL;
  batch->n_runs = 0;
}
