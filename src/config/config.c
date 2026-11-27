#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "config/config.h"
#include "core/core.h"

_Thread_local Config g_cfg_rw;

// --- Enum lookup tables ---

static const EnumEntry direction_table[] = {
    {"minimize", MINIMIZE},
    {"maximize", MAXIMIZE},
    {NULL,       0       },
};

static const EnumEntry budget_table[] = {
    {"iterations",  BUDGET_ITERATIONS },
    {"evaluations", BUDGET_EVALUATIONS},
    {NULL,          0                 },
};

// --- Runner field table ---

// Row order is --help order: [common]-only fields first, [[run]]-only last
const FieldSpec runner_fields[] = {
    FIELD_ROW(Config, seed, UINT, SECTION_COMMON, NULL, NULL,
              "Base seed; replicate r of every run uses seed + r"),
    FIELD_ROW(Config, replicates, INT, SECTION_COMMON, NULL, "1",
              "Times every run is repeated, each with its own seed"),
    FIELD_ROW(Config, direction, ENUM, SCOPE_GLOBAL, direction_table, "minimize",
              "Optimization direction"),
    FIELD_ROW(Config, n_dims, INT, SCOPE_GLOBAL, NULL, "2",
              "Number of dimensions of the search space"),
    FIELD_ROW(Config, budget, ENUM, SCOPE_GLOBAL, budget_table, "iterations",
              "Which cap paces schedules; the other, when set, only stops a run early"),
    FIELD_ROW(Config, iterations, INT, SCOPE_GLOBAL, NULL, "0",
              "Iteration cap (generations, for GA); 0 = off, unless it's the budget"),
    FIELD_ROW(Config, evaluations, INT, SCOPE_GLOBAL, NULL, "0",
              "Fitness evaluation cap; 0 = off, unless it's the budget"),
    FIELD_ROW(Config, target_fitness, DOUBLE, SCOPE_GLOBAL, NULL, "nan",
              "Stop early once the best is at least this good"),
    FIELD_ROW(Config, stagnation, INT, SCOPE_GLOBAL, NULL, "0",
              "Stop early after this many iterations without improving (0 = off)"),
    FIELD_ROW(Config, domain_min, DOUBLE, SCOPE_GLOBAL, NULL, "-5.0",
              "Lower bound of every dimension (-inf = unbounded below)"),
    FIELD_ROW(Config, domain_max, DOUBLE, SCOPE_GLOBAL, NULL, "5.0",
              "Upper bound of every dimension (inf = unbounded above)"),
    FIELD_ROW(Config, init_min, DOUBLE, SCOPE_GLOBAL, NULL, "nan",
              "Lower end of the initial population's range (unset = domain_min)"),
    FIELD_ROW(Config, init_max, DOUBLE, SCOPE_GLOBAL, NULL, "nan",
              "Upper end of the initial population's range (unset = domain_max)"),
    FIELD_ROW(Config, fitness, FITNESS, SCOPE_GLOBAL, NULL, "sphere",
              "Benchmark function to optimize (see --list)"),
    FIELD_ROW(Config, algorithm, ALGORITHM, SECTION_RUN, NULL, NULL,
              "Algorithm to run (see --list)"),
    FIELDS_END,
};

// --- Validation ---

void validate(void) {
  if (!g_cfg.algorithm) die_with_hint("algorithm not set in config file");
  if (g_cfg.n_dims < 1) die_with_hint("n_dims must be at least 1");
  // Either end of the domain may be infinite: the search is unbounded on that side
  // Two finite ends can still be too far apart to subtract
  if (g_cfg.domain_min >= g_cfg.domain_max)
    die_with_hint("domain_min must be strictly less than domain_max");
  if (isfinite(g_cfg.domain_min) && isfinite(g_cfg.domain_max) &&
      !isfinite(g_cfg.domain_max - g_cfg.domain_min))
    die_with_hint("the domain is too wide: domain_max - domain_min must be finite");

  // Not a check: an unset end of the init range is the domain's, resolved here so dump and run
  // see it. An infinite end of the domain can't stand in: the init range is what uniform draws
  // sample from, so it has to be finite, and so does its width
  if (isnan(g_cfg.init_min)) {
    if (isinf(g_cfg.domain_min)) die_with_hint("domain_min is infinite, so init_min must be set");
    g_cfg_rw.init_min = g_cfg.domain_min;
  }
  if (isnan(g_cfg.init_max)) {
    if (isinf(g_cfg.domain_max)) die_with_hint("domain_max is infinite, so init_max must be set");
    g_cfg_rw.init_max = g_cfg.domain_max;
  }
  if (!isfinite(g_cfg.init_min) || !isfinite(g_cfg.init_max))
    die_with_hint("init_min and init_max must be finite");
  if (g_cfg.init_min >= g_cfg.init_max)
    die_with_hint("init_min must be strictly less than init_max");
  if (!isfinite(g_cfg.init_max - g_cfg.init_min))
    die_with_hint("the init range is too wide: init_max - init_min must be finite");
  if (g_cfg.init_min < g_cfg.domain_min || g_cfg.init_max > g_cfg.domain_max)
    die_with_hint("the init range must be inside the domain");

  if (g_cfg.iterations < 0) die_with_hint("iterations must be non-negative (0 disables it)");
  if (g_cfg.evaluations < 0) die_with_hint("evaluations must be non-negative (0 disables it)");
  if (g_cfg.budget == BUDGET_ITERATIONS && g_cfg.iterations == 0)
    die_with_hint("budget is \"iterations\", so iterations must be positive");
  if (g_cfg.budget == BUDGET_EVALUATIONS && g_cfg.evaluations == 0)
    die_with_hint("budget is \"evaluations\", so evaluations must be positive");
  if (g_cfg.stagnation < 0) die_with_hint("stagnation must be non-negative (0 disables it)");

  fitness_check();  // g_cfg.fitness is set: set_field resolved it, or the default did

  if (g_cfg.algorithm->validate) g_cfg.algorithm->validate();
}

// One run's resolved config, as loaded into g_cfg and the algorithm's cfg
void dump_config(void) {
  dump_fields(runner_fields, &g_cfg);
  printf("\n# %s fields\n", g_cfg.algorithm->name);
  dump_fields(g_cfg.algorithm->fields, g_cfg.algorithm->cfg_base());
}
