#include <stdio.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

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
  snprintf(run_src, sizeof(run_src), "%s [[run]]", filename);

  // --- Resolve algorithm first so its defaults land before field overlays ---

  toml_datum_t alg = toml_seek(run0, "algorithm");
  if (alg.type == TOML_UNKNOWN) die_at(run_src, NULL, "[[run]] is missing 'algorithm'");
  if (alg.type != TOML_STRING) die_at(run_src, "algorithm", "expected string");
  select_algorithm(run_src, alg.u.s);

  // --- Apply [common] then [[run]] (last-write-wins) ---
  // The walker also rejects unknown keys and keys in the wrong section

  if (common.type == TOML_TABLE) walk_section(common_src, SECTION_COMMON, common, NULL);
  walk_section(run_src, SECTION_RUN, run0, g_cfg.algorithm);

  // --- Validate ---

  validate();

  // --- Seed: user-set? (scope keeps it in [common]) ---

  int seed_set = (common.type == TOML_TABLE) && (toml_seek(common, "seed").type != TOML_UNKNOWN);
  if (seed_set) rng_init_with(g_cfg.seed);
  else rng_init_auto();

  toml_free(result);
}
