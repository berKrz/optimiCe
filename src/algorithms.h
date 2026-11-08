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

// Pure data row. Walker reads `type` to decide how to parse a datum, consults
// `lookup` for FIELD_ENUM / FIELD_FN, writes the result through
// `base + offset` where `base` is the pointer passed to walk_table
typedef struct FieldSpec {
  const char* name;
  FieldType   type;
  size_t      offset;           // relative to walk_table's `base` arg
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

typedef struct Algorithm {
  const char*      name;
  const FieldSpec* fields;
  void* (*cfg_base)(void);  // returns the address of the algo's _Thread_local cfg
  void (*validate)(void);
  void (*dump)(void);
  void (*run)(void);
} Algorithm;

extern const Algorithm algorithms[];

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

// Walks `table` against `src` (a TOML_TABLE datum), writing each matching field
// through `base + offset`. Pass `&g_cfg` as `base` for common_fields, or the
// return of `g_cfg.algorithm->cfg_base()` for an algorithm's own fields
void walk_table(const char* source, const FieldSpec* table, void* base, toml_datum_t src);

void print_help_from_table(const FieldSpec* table);
void set_defaults(const FieldSpec* table, void* base);

// Resolves `name` against `algorithms[]`, sets g_cfg.algorithm, loads its defaults
// Idempotent: calling twice with the same name is a no-op
// Called by toml.c's algorithm pre-pass and by the walker's FIELD_ALGORITHM case
void select_algorithm(const char* source, const char* name);

// Common fields - defined in config.c
// Declared here because the full FieldSpec type is only visible after this header
extern const FieldSpec common_fields[];

// PSO
extern const FieldSpec pso_fields[];

void* pso_cfg_base(void);
void  pso_run(void);
void  pso_dump(void);
void  pso_validate(void);

// GA
extern const FieldSpec ga_fields[];

void* ga_cfg_base(void);
void  ga_run(void);
void  ga_dump(void);
void  ga_validate(void);

// SMA
extern const FieldSpec sma_fields[];

void* sma_cfg_base(void);
void  sma_run(void);
void  sma_dump(void);
void  sma_validate(void);
