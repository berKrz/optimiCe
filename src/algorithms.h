#pragma once

#include <stddef.h>
#include <tomlc17.h>

#include "common.h"

// User-facing type label for config keys, it describes what shape of value the user may write in a
// config file, and feeds the --help printer and future validators.
typedef enum {
  FIELD_INT,
  FIELD_UINT,
  FIELD_DOUBLE,
  FIELD_BOOL,
  FIELD_ENUM,
  FIELD_FN,
  FIELD_ALGORITHM,
  FIELD_DOUBLE_LIST,  // TOML array of numbers, stored as a DoubleList
} FieldType;

// Storage for FIELD_DOUBLE_LIST. Fixed capacity so the owning config struct stays flat (no
// allocation, copyable by value). The walker rejects arrays longer than DOUBLE_LIST_MAX
#define DOUBLE_LIST_MAX 64

typedef struct {
  int    n;
  double v[DOUBLE_LIST_MAX];
} DoubleList;

// Config sections, one bit each
// The walker is always in exactly one section; a field's scope is the OR of the sections it may
// appear in. --help groups fields by scope, and the walker rejects a key whose scope lacks the
// current section's bit
typedef enum {
  SECTION_COMMON = 1 << 0,  // [common]
  SECTION_RUN    = 1 << 1,  // [[run]]
} Section;

// Named scopes for the masks most rows share
enum {
  SCOPE_GLOBAL = SECTION_COMMON | SECTION_RUN,  // a [[run]] value overrides [common]
  SCOPE_ALGO   = SECTION_RUN,                   // every algorithm field
};

// Pure data row. Walker reads `type` to decide how to parse a datum, consults
// `lookup` for FIELD_ENUM / FIELD_FN, writes the result through
// `base + offset`, where `base` is &g_cfg for runner_fields and the algorithm's cfg_base() for its
// own fields
typedef struct FieldSpec {
  const char* name;
  FieldType   type;
  unsigned    scope;            // OR of the Sections this field may appear in
  size_t      offset;           // relative to the table's base
  const void* lookup;           // FIELD_ENUM / FIELD_FN table, NULL otherwise
  const char* default_display;  // parsed back through the walker in the defaults pass
  const char* description;
} FieldSpec;

// Walker iterates it reading `name`, and writes `value` (as int) through the FieldSpec's
// offset on a match Sentinel: {NULL, 0} terminates the table
typedef struct {
  const char* name;
  int         value;
} EnumEntry;

// One per algorithm, defined in the algorithm's own file
typedef struct Algorithm {
  const char*      name;
  const FieldSpec* fields;
  void* (*cfg_base)(void);  // returns the address of the algo's _Thread_local cfg

  // sizeof the algorithm's cfg. The cfg is copied with memcpy, so it must hold its data by value:
  // no pointers to owned, mutable or parse-tree memory (DoubleList is the pattern); pointers to
  // static constants and functions are fine
  size_t cfg_size;

  // Called once per run, after every field is set and before anything reads the cfg
  // May resolve derived values (e.g. pso's w_end = w), so dump and run see the final config
  void (*validate)(void);
  void (*dump)(void);

  // Fills `out`: fitness, stop state, and n_dims doubles into the caller-owned out->pos
  // Prints only its per-iteration / interactive output; the caller prints the footer
  void (*run)(RunResult* out);
} Algorithm;

extern const Algorithm pso_algorithm;
extern const Algorithm ga_algorithm;
extern const Algorithm sma_algorithm;

// Registry, NULL-terminated - defined in main.c
extern const Algorithm* const algorithms[];

// Reverse lookup: the name a table maps to `value`, or "?" if none does (utils.c)
const char* enum_name(const EnumEntry* table, int value);

// bounds.c - out-of-domain handling shared by continuous-position algorithms
// clamp pins the coordinate to the violated bound, reflect mirrors it back inside (bouncing as
// many times as needed), random redraws it uniformly in the domain
// When a velocity is given (v != NULL), clamp and random zero it and reflect flips its sign on an
// odd number of bounces
typedef enum { BOUNDS_CLAMP, BOUNDS_REFLECT, BOUNDS_RANDOM } BoundsMode;

extern const EnumEntry bounds_table[];

// Brings one coordinate back into [domain_min, domain_max]; no-op when already inside
// `v` is the matching velocity component, or NULL for algorithms without one
void apply_bounds(double* x, double* v, BoundsMode mode);

// Walks the keys of `tab` (a TOML_TABLE datum) in file order, resolving each against runner_fields
// (written into g_cfg) and, when `algo` is given, the algorithm's own fields (written into its cfg)
// Dies on unknown keys and on keys whose scope excludes `section`
// `algorithm` itself is skipped: toml.c's pre-pass resolves it before any walk
void walk_section(const char* source, Section section, toml_datum_t tab, const Algorithm* algo);

// Prints the rows of `table` whose scope is exactly `scope`
void print_help_from_table(const FieldSpec* table, unsigned scope);

// Writes the sections in `scope` as text, e.g. "[common] or [[run]]"
void scope_text(unsigned scope, char* buf, size_t size);
void set_defaults(const FieldSpec* table, void* base);

// Resolves `name` against `algorithms[]`; dies at `source` [algorithm] when it isn't registered
const Algorithm* find_algorithm(const char* source, const char* name);

// Fields that belong to the runner rather than to any algorithm - defined in config.c
// Declared here because the full FieldSpec type is only visible after this header
extern const FieldSpec runner_fields[];
