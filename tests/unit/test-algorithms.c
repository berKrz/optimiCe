#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"

// Every test here runs over every registered algorithm; configurations are built from the field
// tables, so a new algorithm or a new enum value is covered without editing this file

#define N_DIMS     3
#define ITERATIONS 20
#define TOML_LEN   2048

// A short run of `algo` with `extra` appended to its [[run]]
static Batch load_run_of(const Algorithm* algo, const char* extra) {
  char toml[TOML_LEN];
  snprintf(toml, sizeof(toml),
           "[common]\nseed = 7\nn_dims = %d\niterations = %d\n"
           "[[run]]\nalgorithm = \"%s\"\n%s",
           N_DIMS, ITERATIONS, algo->name, extra);
  return load_toml(toml);
}

static void expect_same(const RunResult* a, const RunResult* b, int n_dims, const char* what) {
  cr_expect(eq(dbl, a->fitness, b->fitness), "%s: fitness", what);
  cr_expect(memcmp(a->pos, b->pos, (size_t) n_dims * sizeof(double)) == 0, "%s: position", what);
  cr_expect(eq(dbl, a->stop.best, b->stop.best), "%s: stop.best", what);
  cr_expect(eq(int, a->stop.stale, b->stop.stale), "%s: stop.stale", what);
  cr_expect(eq(int, a->stop.iterations, b->stop.iterations), "%s: stop.iterations", what);
  cr_expect(eq(int, a->stop.reason, b->stop.reason), "%s: stop.reason", what);
  cr_expect(eq(u64, a->evaluations, b->evaluations), "%s: evaluations", what);
}

// Run twice: identical results. Then the result itself: inside the domain, its fitness the
// position's, and its stop state consistent with the budget
static void check_run(const Algorithm* algo, const char* extra) {
  Batch     b     = load_run_of(algo, extra);
  RunResult first = run_job_of(&b, 0), second = run_job_of(&b, 0);
  char      what[TOML_LEN];
  snprintf(what, sizeof(what), "%s with\n%s", algo->name, extra);

  expect_same(&first, &second, N_DIMS, what);

  for (int d = 0; d < N_DIMS; d++)
    cr_expect(first.pos[d] >= g_cfg.domain_min && first.pos[d] <= g_cfg.domain_max,
              "%s: pos[%d] = %g is outside the domain", what, d, first.pos[d]);
  cr_expect(eq(dbl, evaluate(first.pos), first.fitness), "%s: fitness isn't the position's", what);
  cr_expect(first.stop.iterations <= ITERATIONS, "%s: over budget", what);
  cr_expect(ne(int, first.stop.reason, STOP_PENDING), "%s: finished with no reason", what);
  if (first.stop.reason == STOP_ITERATIONS)
    cr_expect(eq(int, first.stop.iterations, ITERATIONS), "%s: stopped early, no reason", what);

  free(first.pos);
  free(second.pos);
  free_batch(&b);
}

// --- Evaluation budget ---

// With no other cap, every algorithm stops on the budget, having made at least its evaluations
Test(algorithms, evaluation_budget) {
  const char* extra = "budget = \"evaluations\"\nevaluations = 300\niterations = 0\n";
  for (int a = 0; algorithms[a]; a++) {
    check_run(algorithms[a], extra);
    Batch     b   = load_run_of(algorithms[a], extra);
    RunResult res = run_job_of(&b, 0);
    cr_expect(eq(int, res.stop.reason, STOP_EVALUATIONS), "%s: stop reason", algorithms[a]->name);
    cr_expect(res.evaluations >= 300, "%s: %" PRIu64 " evaluations, under the budget",
              algorithms[a]->name, res.evaluations);
    free(res.pos);
    free_batch(&b);
  }
}

// --- Every combination of every algorithm's enum fields ---

Test(algorithms, every_enum_combination) {
  for (int a = 0; algorithms[a]; a++) {
    const Algorithm* algo = algorithms[a];
    const FieldSpec* enums[16];
    int              sizes[16], n_enums = 0;
    for (const FieldSpec* f = algo->fields; f->name; f++) {
      if (f->type != FIELD_ENUM) continue;
      const EnumEntry* t = f->lookup;
      sizes[n_enums]     = 0;
      while (t[sizes[n_enums]].name) sizes[n_enums]++;
      enums[n_enums++] = f;
    }

    // A mixed-radix counter over the enum fields: digit k picks the value of enums[k]
    int digit[16] = {0};
    for (;;) {
      char extra[TOML_LEN] = "";
      for (int k = 0; k < n_enums; k++) {
        const EnumEntry* t   = enums[k]->lookup;
        size_t           len = strlen(extra);
        snprintf(extra + len, sizeof(extra) - len, "%s = \"%s\"\n", enums[k]->name,
                 t[digit[k]].name);
      }
      check_run(algo, extra);

      int k = 0;
      while (k < n_enums && ++digit[k] == sizes[k]) digit[k++] = 0;
      if (k == n_enums) break;
    }
  }
}

// --- Threads ---

// Jobs long enough that the threads overlap, all released together by a barrier
#define THREADS 4

typedef struct {
  const Batch* batch;
  int          job;
  RunResult    res;
} Job;

static pthread_mutex_t lock;
static pthread_cond_t  all_ready;
static int             ready;

static void* run_in_thread(void* arg) {
  Job* job = arg;
  pthread_mutex_lock(&lock);
  if (++ready == THREADS) pthread_cond_broadcast(&all_ready);
  while (ready < THREADS) pthread_cond_wait(&all_ready, &lock);
  pthread_mutex_unlock(&lock);
  job->res = run_job_of(job->batch, job->job);
  return NULL;
}

// The first integer field of the algorithm's table, as "key = <twice its default>": a second
// run that differs from the first in the algorithm's own config. Empty when there's none
static void doubled_int_field(const Algorithm* algo, char* out, size_t size) {
  out[0]     = '\0';
  void* base = calloc(1, algo->cfg_size);
  set_defaults(algo->fields, base);
  for (const FieldSpec* f = algo->fields; f->name; f++)
    if (f->type == FIELD_INT) {
      snprintf(out, size, "%s = %d\n", f->name, 2 * *(int*) ((char*) base + f->offset));
      break;
    }
  free(base);
}

// Config, RNG and evaluation counter are all per thread: two different jobs, two threads each,
// all at once, give exactly what each job gives alone
Test(algorithms, jobs_on_threads) {
  for (int a = 0; algorithms[a]; a++) {
    const Algorithm* algo = algorithms[a];
    char             other[128], toml[512];
    doubled_int_field(algo, other, sizeof(other));
    snprintf(toml, sizeof(toml),
             "[common]\nseed = 7\nn_dims = 10\niterations = 2000\n"
             "[[run]]\nalgorithm = \"%s\"\n"
             "[[run]]\nalgorithm = \"%s\"\nn_dims = 9\n%s",
             algo->name, algo->name, other);
    Batch     b       = load_toml(toml);
    RunResult solo[2] = {run_job_of(&b, 0), run_job_of(&b, 1)};
    int       dims[2] = {10, 9};

    Job jobs[THREADS];
    for (int t = 0; t < THREADS; t++) jobs[t] = (Job) {.batch = &b, .job = t % 2};
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&all_ready, NULL);
    ready = 0;
    pthread_t threads[THREADS];
    for (int t = 0; t < THREADS; t++)
      cr_assert(eq(int, pthread_create(&threads[t], NULL, run_in_thread, &jobs[t]), 0));
    for (int t = 0; t < THREADS; t++) pthread_join(threads[t], NULL);
    pthread_cond_destroy(&all_ready);
    pthread_mutex_destroy(&lock);

    for (int t = 0; t < THREADS; t++) {
      char what[64];
      snprintf(what, sizeof(what), "%s, job %d on thread %d", algo->name, t % 2, t);
      expect_same(&solo[t % 2], &jobs[t].res, dims[t % 2], what);
      free(jobs[t].res.pos);
    }
    free(solo[0].pos);
    free(solo[1].pos);
    free_batch(&b);
  }
}

// --- Early stops ---

// A target any first iteration reaches
Test(algorithms, target_stops_after_one_iteration) {
  for (int a = 0; algorithms[a]; a++) {
    Batch     b   = load_run_of(algorithms[a], "target_fitness = 1e300\n");
    RunResult res = run_job_of(&b, 0);
    cr_expect(eq(int, res.stop.reason, STOP_TARGET), "%s", algorithms[a]->name);
    cr_expect(eq(int, res.stop.iterations, 1), "%s", algorithms[a]->name);
    free(res.pos);
    free_batch(&b);
  }
}

// With a long budget every algorithm stalls at some point, and stops on the third stale iteration
Test(algorithms, stagnation_stops) {
  for (int a = 0; algorithms[a]; a++) {
    Batch     b   = load_run_of(algorithms[a], "iterations = 1000\nstagnation = 3\n");
    RunResult res = run_job_of(&b, 0);
    cr_expect(eq(int, res.stop.reason, STOP_STAGNATION), "%s", algorithms[a]->name);
    cr_expect(eq(int, res.stop.stale, 3), "%s", algorithms[a]->name);
    free(res.pos);
    free_batch(&b);
  }
}
