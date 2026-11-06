#include <stdio.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// --- Helpers ---

static int key_in_table(const char* key, const FieldSpec* table) {
  for (int i = 0; table[i].name; i++)
    if (strcmp(key, table[i].name) == 0) return 1;
  return 0;
}

// Dies if `key` is present in `tbl`. Used to enforce "field X must live in
// section Y, not here" schema rules (algorithm in [common], seed in [[run]])
static void require_absent(const char* source, toml_datum_t tbl, const char* key,
                           const char* reason) {
  if (toml_seek(tbl, key).type != TOML_UNKNOWN) die_at(source, key, reason);
}

// Walks the TOML table's own keys, dying on the first one that is not in `t1`
// (or `t2`, if given). `t2` may be NULL for single-table checks
static void check_no_unknown_keys(const char* source, toml_datum_t tbl, const FieldSpec* t1,
                                  const FieldSpec* t2) {
  for (int i = 0; i < tbl.u.tab.size; i++) {
    const char* key = tbl.u.tab.key[i];
    if (key_in_table(key, t1)) continue;
    if (t2 && key_in_table(key, t2)) continue;
    die_at(source, key, "unknown key");
  }
}

// --- Entry point ---

void parse_toml(const char* filename) {
  toml_result_t result = toml_parse_file_ex(filename);
  if (!result.ok) {
    char msg[512];
    snprintf(msg, sizeof(msg), "parse error in '%s': %s", filename, result.errmsg);
    die(msg);
  }

  // TOML parsers reject duplicate keys within a table at parse time

  // --- Top-level: only [common] and [[run]] are allowed ---

  for (int i = 0; i < result.toptab.u.tab.size; i++) {
    const char* key = result.toptab.u.tab.key[i];
    if (strcmp(key, "common") == 0) continue;
    if (strcmp(key, "run") == 0) continue;
    die_at(filename, key, "unknown top-level key (only [common] and [[run]] are allowed)");
  }

  toml_datum_t common = toml_seek(result.toptab, "common");
  toml_datum_t runs   = toml_seek(result.toptab, "run");

  if (common.type != TOML_UNKNOWN && common.type != TOML_TABLE)
    die_at(filename, "common", "must be a table");

  if (runs.type != TOML_ARRAY) die_at(filename, NULL, "missing [[run]] section");
  if (runs.u.arr.size != 1) {
    char msg[128];
    snprintf(msg, sizeof(msg), "only one [[run]] is supported in this version (got %d)",
             runs.u.arr.size);
    die_at(filename, NULL, msg);
  }

  toml_datum_t run0 = runs.u.arr.elem[0];
  if (run0.type != TOML_TABLE) die_at(filename, NULL, "[[run]] entry must be a table");

  char common_src[256], run_src[256];
  snprintf(common_src, sizeof(common_src), "%s [common]", filename);
  snprintf(run_src, sizeof(run_src), "%s [run]", filename);

  // --- Schema checks (fields pinned to one section) ---

  if (common.type == TOML_TABLE)
    require_absent(common_src, common, "algorithm", "'algorithm' must be in [[run]], not [common]");
  require_absent(run_src, run0, "seed", "'seed' must be in [common], not [[run]]");

  // --- Resolve algorithm first so its defaults land before field overlays ---

  toml_datum_t alg = toml_seek(run0, "algorithm");
  if (alg.type == TOML_UNKNOWN) die_at(run_src, NULL, "[[run]] is missing 'algorithm'");
  if (alg.type != TOML_STRING) die_at(run_src, "algorithm", "expected string");
  select_algorithm(run_src, alg.u.s);

  // --- Apply [common] then [[run]] (last-write-wins) ---

  if (common.type == TOML_TABLE) walk_table(common_src, common_fields, &g_cfg, common);
  walk_table(run_src, common_fields, &g_cfg, run0);
  walk_table(run_src, g_cfg.algorithm->fields, g_cfg.algorithm->cfg_base(), run0);

  // --- Unknown-key checks ---

  if (common.type == TOML_TABLE) check_no_unknown_keys(common_src, common, common_fields, NULL);
  check_no_unknown_keys(run_src, run0, common_fields, g_cfg.algorithm->fields);

  // --- Validate ---

  validate();

  // --- Seed: user-set anywhere? (strictly in [common] by schema) ---

  int seed_set = (common.type == TOML_TABLE) && (toml_seek(common, "seed").type != TOML_UNKNOWN);
  if (seed_set) rng_init_with(g_cfg.seed);
  else rng_init_auto();

  toml_free(result);
}
