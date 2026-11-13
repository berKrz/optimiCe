#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "config/config.h"
#include "core/core.h"
#include "io/io.h"

// Runs every job of a loaded Batch and prints its results
// Job j is run j / replicates, replicate j % replicates; results land in results[j], and lines
// are printed in job order (see job_finished), so the output doesn't depend on finishing order
// Jobs run in parallel on min(jobs, CPU cores) threads. A job only touches its own thread's state
// (g_cfg, the algorithm's cfg, the RNG, g_where) and its own results[j]; the Batch is read-only

int batch_jobs(const Batch* batch) { return batch->n_runs * batch->replicates; }

// Copies both config images of a run into this thread's cfgs
static void load_run(const RunConfig* rc) {
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

// Keeps job lines in job order while jobs finish in any order
typedef struct {
  const Batch*     batch;
  const RunResult* results;
  char*            done;    // done[j] = 1 once job j has finished
  int              cursor;  // next job to print
  JobLineWidths    widths;
} Printer;

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
  const Batch* batch = p->batch;
  int          total = batch_jobs(batch);
  p->done[j]         = 1;
  while (p->cursor < total && p->done[p->cursor]) {
    int r = p->cursor % batch->replicates;
    print_job_line(&p->widths, batch->runs[p->cursor / batch->replicates].label, r,
                   job_seed(batch, r), &p->results[p->cursor]);
    write_job_row(batch, p->cursor, &p->results[p->cursor]);
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
    print_batch_header(batch, n_threads);

    Printer printer = {
        .batch = batch, .results = results, .done = done, .widths = job_line_widths(batch)};
    Pool pool = {.batch = batch, .results = results, .printer = &printer};
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
