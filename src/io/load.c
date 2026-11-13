#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/config.h"
#include "core/core.h"
#include "io/io.h"

// Precedence is two independent chains, since [common] can't hold algorithm fields and
// [defaults.<algo>] can't hold runner fields:
//   runner fields:    defaults -> [common]          -> [[run]]
//   algorithm fields: defaults -> [defaults.<algo>] -> [[run]]
// Each chain's base is built once ([common] into common_base, one algo_base per algorithm some
// run uses); every run starts from fresh copies of both and walks its own keys on top

static int n_registered(void) {
  int n = 0;
  while (algorithms[n]) n++;
  return n;
}

// --- Entry point ---

Batch load_batch(const char* filename) {
  toml_result_t result = toml_parse_file_ex(filename);
  if (!result.ok) die_with_hint("parse error in '%s': %s", filename, result.errmsg);

  // TOML parsers reject duplicate keys within a table at parse time

  // --- Top-level: only [common], [defaults.<algo>] and [[run]] are allowed ---

  for (int i = 0; i < result.toptab.u.tab.size; i++) {
    const char* key = result.toptab.u.tab.key[i];
    if (strcmp(key, "common") == 0) continue;
    if (strcmp(key, "defaults") == 0) continue;
    if (strcmp(key, "run") == 0) continue;
    die_at(filename, key,
           "unknown top-level key (only [common], [defaults.<algo>] and [[run]] are allowed)");
  }

  toml_datum_t common   = toml_seek(result.toptab, "common");
  toml_datum_t defaults = toml_seek(result.toptab, "defaults");
  toml_datum_t runs     = toml_seek(result.toptab, "run");

  if (common.type != TOML_UNKNOWN && common.type != TOML_TABLE)
    die_at(filename, "common", "must be a table");
  if (defaults.type != TOML_UNKNOWN && defaults.type != TOML_TABLE)
    die_at(filename, "defaults", "must be a table of [defaults.<algo>] sections");
  if (runs.type != TOML_ARRAY) die_at(filename, NULL, "missing [[run]] section");

  int  n_runs = runs.u.arr.size;
  char src[256];

  // --- Runner base: defaults, then [common] ---

  char common_src[256];
  snprintf(common_src, sizeof(common_src), "%s [common]", filename);

  g_cfg = (Config) {0};
  set_defaults(runner_fields, &g_cfg);
  if (common.type == TOML_TABLE) walk_section(common_src, SECTION_COMMON, common, NULL);
  Config common_base = g_cfg;

  // [common]-only fields can't be overridden, so they're checked once here, located at [common]
  Batch batch = {.n_runs = n_runs, .replicates = common_base.replicates};
  if (batch.replicates < 1) die_at(common_src, "replicates", "must be at least 1");
  if ((long long) n_runs * batch.replicates > INT_MAX)
    die_at(common_src, "replicates", "too many jobs (runs x replicates)");

  batch.seed_generated =
      common.type != TOML_TABLE || toml_seek(common, "seed").type == TOML_UNKNOWN;
  batch.seed       = batch.seed_generated ? rng_generate_seed() : common_base.seed;
  common_base.seed = batch.seed;

  // --- Pre-pass: every run's algorithm and label, before any run's fields are walked ---

  int  n_algos  = n_registered();
  int* run_algo = xmalloc((size_t) n_runs * sizeof(int));
  int* count    = xcalloc((size_t) n_algos, sizeof(int));  // runs per algorithm; 0 = unused
  batch.runs    = xcalloc((size_t) n_runs, sizeof(RunConfig));

  for (int k = 0; k < n_runs; k++) {
    // No label yet: until the algorithm is known, a run is named by its position
    snprintf(src, sizeof(src), "%s [[run]] #%d", filename, k + 1);
    toml_datum_t run = runs.u.arr.elem[k];
    if (run.type != TOML_TABLE) die_at(src, NULL, "[[run]] entry must be a table");

    toml_datum_t alg = toml_seek(run, "algorithm");
    if (alg.type == TOML_UNKNOWN) die_at(src, NULL, "[[run]] is missing 'algorithm'");
    if (alg.type != TOML_STRING) die_at(src, "algorithm", "expected string");
    int a = find_algorithm(alg.u.s);
    if (a < 0) die_at(src, "algorithm", "unknown algorithm");

    run_algo[k] = a;
    snprintf(batch.runs[k].label, sizeof(batch.runs[k].label), "%s-%d", algorithms[a]->name,
             ++count[a]);
  }

  // --- Algorithm bases: defaults, then [defaults.<algo>], for algorithms some run uses ---

  for (int a = 0; a < n_algos; a++)
    if (count[a]) set_defaults(algorithms[a]->fields, algorithms[a]->cfg_base());

  if (defaults.type == TOML_TABLE) {
    char defaults_src[256];
    snprintf(defaults_src, sizeof(defaults_src), "%s [defaults]", filename);

    for (int i = 0; i < defaults.u.tab.size; i++) {
      const char*  key = defaults.u.tab.key[i];
      toml_datum_t tab = defaults.u.tab.value[i];
      int          a   = find_algorithm(key);
      if (a < 0) die_at(defaults_src, key, "unknown algorithm");
      if (tab.type != TOML_TABLE) die_at(defaults_src, key, "must be a table");
      if (!count[a]) continue;  // no run uses this algorithm: the block isn't walked

      snprintf(src, sizeof(src), "%s [defaults.%s]", filename, key);
      walk_section(src, SECTION_DEFAULTS, tab, algorithms[a]);
    }
  }

  void** algo_base = xcalloc((size_t) n_algos, sizeof(void*));
  for (int a = 0; a < n_algos; a++) {
    if (!count[a]) continue;
    algo_base[a] = xmalloc(algorithms[a]->cfg_size);
    memcpy(algo_base[a], algorithms[a]->cfg_base(), algorithms[a]->cfg_size);
  }

  // --- Runs: fresh copies of both bases, then the run's own keys, then validate ---

  for (int k = 0; k < n_runs; k++) {
    RunConfig*       rc   = &batch.runs[k];
    const Algorithm* algo = algorithms[run_algo[k]];
    snprintf(src, sizeof(src), "%s [[run]] %s", filename, rc->label);

    g_cfg           = common_base;
    g_cfg.algorithm = algo;
    memcpy(algo->cfg_base(), algo_base[run_algo[k]], algo->cfg_size);

    // The walker also rejects unknown keys and keys in the wrong section
    walk_section(src, SECTION_RUN, runs.u.arr.elem[k], algo);

    g_where = src;
    validate();
    g_where = NULL;

    // Saved after validate, which may resolve derived values
    rc->cfg      = g_cfg;
    rc->algo_cfg = xmalloc(algo->cfg_size);
    memcpy(rc->algo_cfg, algo->cfg_base(), algo->cfg_size);
  }

  for (int a = 0; a < n_algos; a++) free(algo_base[a]);
  free(algo_base);
  free(count);
  free(run_algo);
  toml_free(result);

  return batch;
}
