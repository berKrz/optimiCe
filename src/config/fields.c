#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/config.h"
#include "core/core.h"

static void         set_field(const char* source, const FieldSpec* row, void* base, toml_datum_t d);
static toml_datum_t default_as_datum(const FieldSpec* row);

// --- set_field: the single write path shared by walker and defaults ---

// A "nan" default marks an optional float: NaN means unset, and is resolved after parsing if at
// all (e.g. pso's w_end = w). Only these fields accept nan, from a default or from the file, where
// it turns the field back off in a [[run]]
static int is_optional(const FieldSpec* row) {
  return row->default_display && strcmp(row->default_display, "nan") == 0;
}

// The number in a float or integer datum; dies on any other type, and on nan
static double number_of(const char* source, const FieldSpec* row, toml_datum_t d, int allow_nan) {
  double v;
  if (d.type == TOML_FP64) v = d.u.fp64;
  else if (d.type == TOML_INT64) v = (double) d.u.int64;
  else die_at(source, row->name, "expected a number");
  if (isnan(v) && !allow_nan) die_at(source, row->name, "nan is not a valid value");
  return v;
}

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
      *(double*) target = number_of(source, row, d, is_optional(row));
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
    case FIELD_FITNESS: {
      // Stores the catalog entry itself; fitness.c owns the only catalog, so the row's lookup is
      // unused
      if (d.type != TOML_STRING) die_at(source, row->name, "expected string");
      const FitnessEntry* entry = fitness_find(d.u.s);
      if (!entry) die_at(source, row->name, "unknown fitness function");
      *(const FitnessEntry**) target = entry;
      return;
    }
    case FIELD_ALGORITHM:
      // Unreachable: walk_section skips the algorithm row, which load.c's pre-pass resolves
      die("internal: FIELD_ALGORITHM reached set_field");
    case FIELD_DOUBLE_LIST: {
      if (d.type != TOML_ARRAY) die_at(source, row->name, "expected array of numbers");
      if (d.u.arr.size > DOUBLE_LIST_MAX)
        die_at(source, row->name, "too many elements (max %d)", DOUBLE_LIST_MAX);
      // Fill a local copy first so a bad element leaves the target untouched
      DoubleList list = {.n = d.u.arr.size};
      for (int i = 0; i < list.n; i++) list.v[i] = number_of(source, row, d.u.arr.elem[i], 0);
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
    case FIELD_FITNESS: {
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

static const FieldSpec* find_row(const FieldSpec* table, const char* key) {
  for (int i = 0; table[i].name; i++)
    if (strcmp(key, table[i].name) == 0) return &table[i];
  return NULL;
}

void scope_text(unsigned scope, char* buf, size_t size) {
  static const struct {
    unsigned    bit;
    const char* name;
  } names[] = {
      {SECTION_COMMON,   "[common]"         },
      {SECTION_RUN,      "[[run]]"          },
      {SECTION_DEFAULTS, "[defaults.<algo>]"},
  };
  size_t n = 0;
  buf[0]   = '\0';
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    if (!(scope & names[i].bit)) continue;
    n +=
        (size_t) snprintf(buf + n, n < size ? size - n : 0, "%s%s", n ? " or " : "", names[i].name);
  }
}

// Unknown-key error, with a hint when the key exists somewhere else
static void die_unknown(const char* source, const char* key, const Algorithm* algo) {
  if (algo) die_at(source, key, "unknown key for algorithm '%s'", algo->name);
  for (int i = 0; algorithms[i]; i++) {
    if (find_row(algorithms[i]->fields, key)) {
      char where[64];
      scope_text(SCOPE_ALGO, where, sizeof(where));
      die_at(source, key, "'%s' is a field of algorithm '%s'; algorithm fields go in %s", key,
             algorithms[i]->name, where);
    }
  }
  die_at(source, key, "unknown key");
}

void walk_section(const char* source, Section section, toml_datum_t tab, const Algorithm* algo) {
  for (int i = 0; i < tab.u.tab.size; i++) {
    const char*      key  = tab.u.tab.key[i];
    const FieldSpec* row  = find_row(runner_fields, key);
    const FieldSpec* arow = algo ? find_row(algo->fields, key) : NULL;
    void*            base = &g_cfg;

    if (row && arow)
      die_at(source, key, "internal: key defined by both the runner and the algorithm");
    if (arow) {
      row  = arow;
      base = algo->cfg_base();
    }
    if (!row) die_unknown(source, key, algo);

    if (!(row->scope & section)) {
      char where[64];
      scope_text(row->scope, where, sizeof(where));
      die_at(source, key, "only allowed in %s", where);
    }

    if (row->type == FIELD_ALGORITHM) continue;  // resolved by load.c's pre-pass
    set_field(source, row, base, tab.u.tab.value[i]);
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

// --- Help printer ---

static const char* type_label(FieldType t) {
  switch (t) {
    case FIELD_INT:         return "int";
    case FIELD_UINT:        return "uint32";
    case FIELD_DOUBLE:      return "float";
    case FIELD_BOOL:        return "bool";
    case FIELD_ENUM:        return "enum";
    case FIELD_FITNESS:     return "name";
    case FIELD_ALGORITHM:   return "name";
    case FIELD_DOUBLE_LIST: return "float[]";
  }
  return "?";
}

static const char* get_default_display(const FieldSpec* f) {
  if (is_optional(f)) return "(unset)";
  if (f->default_display) return f->default_display;
  if (strcmp(f->name, "algorithm") == 0) return "(required)";
  if (strcmp(f->name, "seed") == 0) return "(generated)";
  return "-";
}

// An enum row's description is followed by its choices, read from its lookup table:
// "Selection: roulette | rank | tournament"
void print_help_from_table(const FieldSpec* table, unsigned scope) {
  for (int i = 0; table[i].name; i++) {
    const FieldSpec* row = &table[i];
    if (row->scope != scope) continue;
    printf("  %-16s  %-7s  (default: %-14s)  %s", row->name, type_label(row->type),
           get_default_display(row), row->description ? row->description : "");
    if (row->type == FIELD_ENUM) {
      const EnumEntry* choices = row->lookup;
      for (int c = 0; choices[c].name; c++) printf("%s%s", c ? " | " : ": ", choices[c].name);
    }
    printf("\n");
  }
}

// --- Dump ---

const char* enum_name(const EnumEntry* table, int value) {
  for (int i = 0; table[i].name; i++)
    if (table[i].value == value) return table[i].name;
  return "?";
}

// Prints `name = value` for the rows of `table` a run can set, reading each value through
// `base + offset` by the row's type: the reverse of set_field. Names are padded to the longest
// [common]-only rows (seed, replicates) are batch-wide and skipped; dump_batch prints them
void dump_fields(const FieldSpec* table, const void* base) {
  int width = 0;
  for (int i = 0; table[i].name; i++) {
    int len = (int) strlen(table[i].name);
    if ((table[i].scope & SECTION_RUN) && len > width) width = len;
  }

  for (int i = 0; table[i].name; i++) {
    const FieldSpec* row = &table[i];
    if (!(row->scope & SECTION_RUN)) continue;
    const void* value = (const char*) base + row->offset;

    printf("%-*s = ", width, row->name);
    switch (row->type) {
      case FIELD_INT:    printf("%d", *(const int*) value); break;
      case FIELD_UINT:   printf("0x%08x", *(const uint32_t*) value); break;
      case FIELD_DOUBLE: printf("%g", *(const double*) value); break;
      case FIELD_BOOL:   printf("%s", *(const int*) value ? "true" : "false"); break;
      case FIELD_ENUM:   printf("%s", enum_name(row->lookup, *(const int*) value)); break;
      case FIELD_FITNESS:
        printf("%s", fitness_name(*(const FitnessEntry* const*) value));
        break;
      case FIELD_ALGORITHM: printf("%s", (*(const Algorithm* const*) value)->name); break;
      case FIELD_DOUBLE_LIST: {
        const DoubleList* list = value;
        printf("[");
        for (int k = 0; k < list->n; k++) printf(k ? ", %g" : "%g", list->v[k]);
        printf("]");
        break;
      }
    }
    printf("\n");
  }
}
