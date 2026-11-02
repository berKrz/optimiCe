#pragma once

#include "common.h"

// User-facing type label for config keys, it describes what shape of value the user may write in a
// config file, and feeds the --help printer and future validators.
typedef enum {
  FIELD_INT,
  FIELD_UINT64,
  FIELD_DOUBLE,
  FIELD_BOOL,
  FIELD_ENUM,
  FIELD_FN,
} FieldType;

typedef struct FieldSpec {
  const char* name;
  FieldType   type;
  const char* default_display;
  const char* description;
  void (*apply)(const char* source, const char* key, const char* val);
} FieldSpec;

typedef struct Algorithm {
  const char*      name;
  AlgorithmKind    kind;
  const FieldSpec* fields;
  void (*run)(void);
} Algorithm;

extern const Algorithm algorithms[];

void print_help_from_table(const FieldSpec* table);
void set_defaults_from_table(const FieldSpec* table);

// Common fields - defined in config.c
// Declared here because the full FieldSpec type is only visible after this header
extern const FieldSpec common_fields[];

// PSO
extern const FieldSpec pso_fields[];
void                   pso_run(void);
