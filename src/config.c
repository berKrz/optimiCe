#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// Tracks whether `algorithm = ...` has been seen in the current parse.
// Flipped by apply_algorithm; read by apply_field to enforce
// "algorithm must be the first non-comment key, and must appear exactly once".
// _Thread_local so parallel runs in Step 3 don't share this state.
static _Thread_local int algorithm_seen = 0;

// --- Handlers ---

static void apply_algorithm(const char* source, const char* key, const char* val) {
  if (algorithm_seen) die_at(source, key, "'algorithm' já foi definido");

  for (int i = 0; algorithms[i].name; i++) {
    if (strcmp(val, algorithms[i].name) == 0) {
      g_cfg.algorithm = &algorithms[i];
      set_defaults_from_table(algorithms[i].fields);
      algorithm_seen = 1;
      return;
    }
  }
  die_at(source, key, "algoritmo desconhecido");
}

static void apply_seed(const char* source, const char* key, const char* val) {
  char* end;
  errno           = 0;
  unsigned long v = strtoul(val, &end, 0);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v > UINT32_MAX)
    die_at(source, key, "deve ser um inteiro sem sinal");
  rng_init_with(v);
}

static void apply_direction(const char* source, const char* key, const char* val) {
  if (strcmp(val, "minimize") == 0) g_cfg.direction = MINIMIZE;
  else if (strcmp(val, "maximize") == 0) g_cfg.direction = MAXIMIZE;
  else die_at(source, key, "deve ser 'minimize' ou 'maximize'");
}

static void apply_n_dims(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v <= 0 || v > INT_MAX)
    die_at(source, key, "deve ser um inteiro positivo");
  g_cfg.n_dims = (int) v;
}

static void apply_domain_min(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE)
    die_at(source, key, "deve ser um número de ponto flutuante");
  g_cfg.domain_min = v;
}

static void apply_domain_max(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE)
    die_at(source, key, "deve ser um número de ponto flutuante");
  g_cfg.domain_max = v;
}

static void apply_fitness(const char* source, const char* key, const char* val) {
  for (int i = 0; fitness_table[i].name; i++) {
    if (strcmp(val, fitness_table[i].name) == 0) {
      g_cfg.fitness_fn = fitness_table[i].fn;
      return;
    }
  }
  die_at(source, key, "função de aptidão desconhecida");
}

// --- Common field table ---

const FieldSpec common_fields[] = {
    {"algorithm",  FIELD_ENUM,   NULL,       "Algoritmo a executar: pso | ga",    apply_algorithm },
    {"seed",       FIELD_UINT,   NULL,       "Semente do RNG (ausente → gerada)", apply_seed      },
    {"direction",  FIELD_ENUM,   "minimize", "Sentido: minimize | maximize",      apply_direction },
    {"n_dims",     FIELD_INT,    "2",        "Número de dimensões",               apply_n_dims    },
    {"domain_min", FIELD_DOUBLE, "-5.0",     "Limite inferior do domínio",        apply_domain_min},
    {"domain_max", FIELD_DOUBLE, "5.0",      "Limite superior do domínio",        apply_domain_max},
    {"fitness",    FIELD_FN,     "sphere",   "Função de aptidão",                 apply_fitness   },
    {NULL,         0,            NULL,       NULL,                                NULL            },
};

// --- Walkers ---

void set_defaults_from_table(const FieldSpec* table) {
  for (int i = 0; table[i].name; i++)
    if (table[i].default_display)
      table[i].apply("default", table[i].name, table[i].default_display);
}

static const char* field_type_name(FieldType t) {
  switch (t) {
    case FIELD_INT:    return "int";
    case FIELD_UINT:   return "uint64";
    case FIELD_DOUBLE: return "float";
    case FIELD_BOOL:   return "bool";
    case FIELD_ENUM:   return "enum";
    case FIELD_FN:     return "name";
  }
  return "?";
}

static const char* default_display_for(const FieldSpec* f) {
  if (f->default_display) return f->default_display;
  if (strcmp(f->name, "algorithm") == 0) return "(obrigatório)";
  if (strcmp(f->name, "seed") == 0) return "(gerada)";
  return "-";
}

void print_help_from_table(const FieldSpec* table) {
  for (int i = 0; table[i].name; i++) {
    printf("  %-16s  %-7s  (padrão: %-14s)  %s\n", table[i].name, field_type_name(table[i].type),
           default_display_for(&table[i]), table[i].description ? table[i].description : "");
  }
}

// --- Dispatch ---

void apply_field(const char* source, const char* key, const char* val) {
  for (int i = 0; common_fields[i].name; i++) {
    if (strcmp(key, common_fields[i].name) == 0) {
      common_fields[i].apply(source, key, val);
      return;
    }
  }

  if (!algorithm_seen) die_at(source, key, "'algorithm' deve ser a primeira chave do arquivo");

  const FieldSpec* tbl = g_cfg.algorithm->fields;
  for (int i = 0; tbl[i].name; i++) {
    if (strcmp(key, tbl[i].name) == 0) {
      tbl[i].apply(source, key, val);
      return;
    }
  }

  die_at(source, key, "chave desconhecida para o algoritmo selecionado");
}

// --- Validation ---

// Reverse lookup: FitnessEntry for the fn pointer currently in g_cfg.
// Called once per run from validate() and dump_config_fn(); cold-path cost.
static const FitnessEntry* fitness_entry_for(double (*fn)(const double*)) {
  for (int i = 0; fitness_table[i].name; i++)
    if (fitness_table[i].fn == fn) return &fitness_table[i];
  return NULL;  // apply_fitness guarantees a match; unreachable
}

void validate(void) {
  if (g_cfg.domain_min >= g_cfg.domain_max)
    die("domain_min deve ser estritamente menor que domain_max");

  const FitnessEntry* fe = fitness_entry_for(g_cfg.fitness_fn);
  if (!fe) die("função de aptidão não encontrada no catálogo");
  char msg[256];

  if (fe->n_dims_min && g_cfg.n_dims < fe->n_dims_min) {
    snprintf(msg, sizeof(msg), "para %s, n_dims deve ser pelo menos %d (atual: %d)", fe->name,
             fe->n_dims_min, g_cfg.n_dims);
    die(msg);
  }
  if (fe->n_dims_max && g_cfg.n_dims > fe->n_dims_max) {
    snprintf(msg, sizeof(msg), "para %s, n_dims deve ser no máximo %d (atual: %d)", fe->name,
             fe->n_dims_max, g_cfg.n_dims);
    die(msg);
  }
  if (g_cfg.domain_min < fe->domain_lo || g_cfg.domain_max > fe->domain_hi) {
    snprintf(msg, sizeof(msg), "para %s, o domínio deve estar contido em [%g, %g]", fe->name,
             fe->domain_lo, fe->domain_hi);
    die(msg);
  }

  if (g_cfg.algorithm->validate) g_cfg.algorithm->validate();
}

// --- Dump ---

void dump_config_fn(void) {
  const FitnessEntry* fe = fitness_entry_for(g_cfg.fitness_fn);

  printf("# resolved g_cfg\n");
  printf("algorithm   = %s\n", g_cfg.algorithm ? g_cfg.algorithm->name : "(unset)");
  printf("seed = 0x%08x\n", g_cfg.seed);
  printf("direction   = %s\n", g_cfg.direction == MINIMIZE ? "minimize" : "maximize");
  printf("n_dims      = %d\n", g_cfg.n_dims);
  printf("domain_min  = %g\n", g_cfg.domain_min);
  printf("domain_max  = %g\n", g_cfg.domain_max);
  printf("interactive = %s\n", g_cfg.interactive ? "true" : "false");
  printf("fitness     = %s\n", fe ? fe->name : "(unknown)");

  if (!g_cfg.algorithm) return;
  printf("\n# %s arm\n", g_cfg.algorithm->name);
}
