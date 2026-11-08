#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

static void         set_field(const char* source, const FieldSpec* row, void* base, toml_datum_t d);
static toml_datum_t default_as_datum(const FieldSpec* row);

// --- Enum lookup tables ---

static const EnumEntry direction_table[] = {
    {"minimize", MINIMIZE},
    {"maximize", MAXIMIZE},
    {NULL,       0       },
};

// --- Reverse lookups (cold path: validate() and dump_config()) ---

static const FitnessEntry* get_fitness_entry(double (*fn)(const double*)) {
  for (int i = 0; fitness_table[i].name; i++)
    if (fitness_table[i].fn == fn) return &fitness_table[i];
  return NULL;  // set_field guarantees a match; unreachable
}

// --- Algorithm selection ---

void select_algorithm(const char* source, const char* name) {
  for (int i = 0; algorithms[i].name; i++) {
    if (strcmp(name, algorithms[i].name) == 0) {
      if (g_cfg.algorithm == &algorithms[i]) return;  // idempotent re-select
      g_cfg.algorithm = &algorithms[i];
      set_defaults(algorithms[i].fields, algorithms[i].cfg_base());
      return;
    }
  }
  die_at(source, "algorithm", "unknown algorithm");
}

// --- set_field: the single write path shared by walker and defaults ---

static void set_field(const char* source, const FieldSpec* row, void* base, toml_datum_t d) {
  uint8_t* target = (uint8_t*) base + row->offset;
  switch (row->type) {
    case FIELD_INT: {
      if (d.type != TOML_INT64) die_at(source, row->name, "expected integer");
      if (d.u.int64 < INT_MIN || d.u.int64 > INT_MAX) die_at(source, row->name, "out of int range");
      *(int*) target = (int) d.u.int64;
      return;
    }
    case FIELD_UINT: {
      if (d.type != TOML_INT64) die_at(source, row->name, "expected integer");
      if (d.u.int64 < 0 || d.u.int64 > UINT32_MAX) die_at(source, row->name, "out of uint32 range");
      *(uint32_t*) target = (uint32_t) d.u.int64;
      return;
    }
    case FIELD_DOUBLE: {
      if (d.type == TOML_FP64) *(double*) target = d.u.fp64;
      else if (d.type == TOML_INT64) *(double*) target = (double) d.u.int64;
      else die_at(source, row->name, "expected floating-point number");
      return;
    }
    case FIELD_BOOL: {
      if (d.type != TOML_BOOLEAN) die_at(source, row->name, "expected true or false");
      *(int*) target = d.u.boolean ? 1 : 0;
      return;
    }
    case FIELD_ENUM: {
      // Enum-typed struct members are written as int - assumes enums are int-sized, which holds
      // on GCC/Clang unless -fshort-enums is passed
      if (d.type != TOML_STRING) die_at(source, row->name, "expected string");
      const EnumEntry* t = row->lookup;
      for (int i = 0; t[i].name; i++) {
        if (strcmp(t[i].name, d.u.s) == 0) {
          *(int*) target = t[i].value;
          return;
        }
      }
      die_at(source, row->name, "unrecognized value");
    }
    case FIELD_FN: {
      // fitness_table is the only FIELD_FN consumer in the project
      if (d.type != TOML_STRING) die_at(source, row->name, "expected string");
      for (int i = 0; fitness_table[i].name; i++) {
        if (strcmp(fitness_table[i].name, d.u.s) == 0) {
          // memcpy to avoid writing a function pointer through a differently-typed pointer
          double (*fn)(const double*) = fitness_table[i].fn;
          memcpy(target, &fn, sizeof(fn));
          return;
        }
      }
      die_at(source, row->name, "unknown fitness function");
    }
    case FIELD_ALGORITHM: {
      if (d.type != TOML_STRING) die_at(source, row->name, "expected string");
      select_algorithm(source, d.u.s);
      return;
    }
    case FIELD_DOUBLE_LIST: {
      if (d.type != TOML_ARRAY) die_at(source, row->name, "expected array of numbers");
      if (d.u.arr.size > DOUBLE_LIST_MAX) {
        char msg[64];
        snprintf(msg, sizeof(msg), "too many elements (max %d)", DOUBLE_LIST_MAX);
        die_at(source, row->name, msg);
      }
      // Fill a local copy first so a bad element leaves the target untouched
      DoubleList list = {.n = d.u.arr.size};
      for (int i = 0; i < list.n; i++) {
        toml_datum_t e = d.u.arr.elem[i];
        if (e.type == TOML_FP64) list.v[i] = e.u.fp64;
        else if (e.type == TOML_INT64) list.v[i] = (double) e.u.int64;
        else die_at(source, row->name, "expected array of numbers");
      }
      memcpy(target, &list, sizeof(list));
      return;
    }
  }
}

// --- Synthesis of default datums ---

static toml_datum_t default_as_datum(const FieldSpec* row) {
  toml_datum_t d = {0};
  switch (row->type) {
    case FIELD_INT:
    case FIELD_UINT: {
      char* end;
      errno       = 0;
      long long v = strtoll(row->default_display, &end, 0);
      if (*row->default_display == '\0' || *end != '\0' || errno == ERANGE)
        die_at("default", row->name, "malformed default_display");
      d.type    = TOML_INT64;
      d.u.int64 = v;
      return d;
    }
    case FIELD_DOUBLE: {
      char* end;
      errno    = 0;
      double v = strtod(row->default_display, &end);
      if (*row->default_display == '\0' || *end != '\0' || errno == ERANGE)
        die_at("default", row->name, "malformed default_display");
      d.type   = TOML_FP64;
      d.u.fp64 = v;
      return d;
    }
    case FIELD_BOOL: {
      d.type = TOML_BOOLEAN;
      if (strcmp(row->default_display, "true") == 0) d.u.boolean = 1;
      else if (strcmp(row->default_display, "false") == 0) d.u.boolean = 0;
      else die_at("default", row->name, "malformed default_display (expected true or false)");
      return d;
    }
    case FIELD_ENUM:
    case FIELD_FN:   {
      d.type = TOML_STRING;
      // default_display is a string literal; set_field only reads .u.s,
      // and the synthesized datum never reaches tomlc17's free path
      d.u.s = (char*) row->default_display;
      return d;
    }
    case FIELD_ALGORITHM:
      // Unreachable: caller (set_defaults) skips rows with NULL default_display,
      // and FIELD_ALGORITHM has no default
      die("internal: FIELD_ALGORITHM has no default");
    case FIELD_DOUBLE_LIST:
      // Unreachable: set_defaults routes list rows through set_list_default, since an array
      // datum needs a parse result that outlives this function
      die("internal: FIELD_DOUBLE_LIST default must go through set_list_default");
  }
  return d;  // unreachable
}

// --- Public walkers ---

void walk_table(const char* source, const FieldSpec* table, void* base, toml_datum_t src) {
  for (int i = 0; table[i].name; i++) {
    toml_datum_t d = toml_seek(src, table[i].name);
    if (d.type == TOML_UNKNOWN) continue;  // not set-> leave target alone
    set_field(source, &table[i], base, d);
  }
}

// List defaults are written in TOML array syntax ("[0.3, 0.7]") and parsed by tomlc17 itself,
// wrapped as "v = <default>"
// The parse result owns the array, so it's freed only after set_field has copied the values out
static void set_list_default(const FieldSpec* row, void* base) {
  char src[256];
  int  len = snprintf(src, sizeof(src), "v = %s", row->default_display);
  if (len < 0 || len >= (int) sizeof(src)) die_at("default", row->name, "default_display too long");

  toml_result_t res = toml_parse(src, len);
  if (!res.ok) die_at("default", row->name, "malformed default_display (expected TOML array)");
  set_field("default", row, base, toml_seek(res.toptab, "v"));
  toml_free(res);
}

void set_defaults(const FieldSpec* table, void* base) {
  for (int i = 0; table[i].name; i++) {
    if (!table[i].default_display) continue;
    if (table[i].type == FIELD_DOUBLE_LIST) {
      set_list_default(&table[i], base);
      continue;
    }
    toml_datum_t d = default_as_datum(&table[i]);
    set_field("default", &table[i], base, d);
  }
}

// --- Common field table ---

const FieldSpec common_fields[] = {
    {"algorithm", FIELD_ALGORITHM, 0, NULL, NULL, "Algorithm to run: pso | ga | sma"},
    {"seed", FIELD_UINT, offsetof(Config, seed), NULL, NULL, "RNG seed (omitted = generated)"},
    {"direction", FIELD_ENUM, offsetof(Config, direction), direction_table, "minimize",
     "Direction: minimize | maximize"},
    {"n_dims", FIELD_INT, offsetof(Config, n_dims), NULL, "2", "Number of dimensions"},
    {"iterations", FIELD_INT, offsetof(Config, iterations), NULL, "100",
     "Iteration budget (generations, for GA)"},
    {"target_fitness", FIELD_DOUBLE, offsetof(Config, target_fitness), NULL, "nan",
     "Stop early once the best reaches this value"},
    {"stagnation", FIELD_INT, offsetof(Config, stagnation), NULL, "0",
     "Stop early after N iterations without improvement (0 = off)"},
    {"domain_min", FIELD_DOUBLE, offsetof(Config, domain_min), NULL, "-5.0",
     "Lower bound of the domain"},
    {"domain_max", FIELD_DOUBLE, offsetof(Config, domain_max), NULL, "5.0",
     "Upper bound of the domain"},
    {"fitness", FIELD_FN, offsetof(Config, fitness_fn), NULL, "sphere", "Fitness function"},
    {NULL, 0, 0, NULL, NULL, NULL},
};

// --- Help printer ---

static const char* type_label(FieldType t) {
  switch (t) {
    case FIELD_INT:         return "int";
    case FIELD_UINT:        return "uint32";
    case FIELD_DOUBLE:      return "float";
    case FIELD_BOOL:        return "bool";
    case FIELD_ENUM:        return "enum";
    case FIELD_FN:          return "name";
    case FIELD_ALGORITHM:   return "name";
    case FIELD_DOUBLE_LIST: return "float[]";
  }
  return "?";
}

static const char* get_default_display(const FieldSpec* f) {
  // A "nan" default marks an optional float that's resolved after parsing (e.g. w_end = w)
  if (f->default_display && strcmp(f->default_display, "nan") == 0) return "(unset)";
  if (f->default_display) return f->default_display;
  if (strcmp(f->name, "algorithm") == 0) return "(required)";
  if (strcmp(f->name, "seed") == 0) return "(generated)";
  return "-";
}

void print_help_from_table(const FieldSpec* table) {
  for (int i = 0; table[i].name; i++) {
    printf("  %-16s  %-7s  (default: %-14s)  %s\n", table[i].name, type_label(table[i].type),
           get_default_display(&table[i]), table[i].description ? table[i].description : "");
  }
}

// --- Validation ---

void validate(void) {
  if (!g_cfg.algorithm) die("algorithm not set in config file");
  if (g_cfg.domain_min >= g_cfg.domain_max) die("domain_min must be strictly less than domain_max");
  if (g_cfg.iterations < 1) die("iterations must be positive");
  if (g_cfg.stagnation < 0) die("stagnation must be non-negative (0 disables it)");

  const FitnessEntry* fe = get_fitness_entry(g_cfg.fitness_fn);
  if (!fe) die("fitness function not found in catalog");
  char msg[256];

  if (fe->n_dims_min && g_cfg.n_dims < fe->n_dims_min) {
    snprintf(msg, sizeof(msg), "for %s, n_dims must be at least %d (current: %d)", fe->name,
             fe->n_dims_min, g_cfg.n_dims);
    die(msg);
  }
  if (fe->n_dims_max && g_cfg.n_dims > fe->n_dims_max) {
    snprintf(msg, sizeof(msg), "for %s, n_dims must be at most %d (current: %d)", fe->name,
             fe->n_dims_max, g_cfg.n_dims);
    die(msg);
  }
  if (g_cfg.domain_min < fe->domain_lo || g_cfg.domain_max > fe->domain_hi) {
    snprintf(msg, sizeof(msg), "for %s, the domain must be contained in [%g, %g]", fe->name,
             fe->domain_lo, fe->domain_hi);
    die(msg);
  }

  if (g_cfg.algorithm->validate) g_cfg.algorithm->validate();
}

// --- Dump ---

void dump_config(void) {
  const FitnessEntry* fe = get_fitness_entry(g_cfg.fitness_fn);

  printf("# resolved config\n");
  printf("algorithm      = %s\n", g_cfg.algorithm ? g_cfg.algorithm->name : "(unset)");
  printf("seed           = 0x%08x\n", g_cfg.seed);
  printf("direction      = %s\n", enum_name(direction_table, g_cfg.direction));
  printf("n_dims         = %d\n", g_cfg.n_dims);
  printf("iterations     = %d\n", g_cfg.iterations);
  printf("target_fitness = %g\n", g_cfg.target_fitness);
  printf("stagnation     = %d\n", g_cfg.stagnation);
  printf("domain_min     = %g\n", g_cfg.domain_min);
  printf("domain_max     = %g\n", g_cfg.domain_max);
  printf("interactive    = %s\n", g_cfg.interactive ? "true" : "false");
  printf("fitness        = %s\n", fe ? fe->name : "(unknown)");

  if (!g_cfg.algorithm) return;
  printf("\n# %s arm\n", g_cfg.algorithm->name);
  if (g_cfg.algorithm->dump) g_cfg.algorithm->dump();  // defensive
}
