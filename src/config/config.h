#pragma once

// The config field engine (fields.c) and the runner's own fields (config.c)

#include <tomlc17.h>

#include "algorithms/algorithm.h"

// --- Field engine (fields.c) ---

// Walks the keys of `tab` (a TOML_TABLE datum) in file order, resolving each against runner_fields
// (written into g_cfg) and, when `algo` is given, the algorithm's own fields (written into its cfg)
// `algo` is given for [[run]] and [defaults.<algo>], NULL for [common]
// Dies on unknown keys and on keys whose scope excludes `section`
// `algorithm` itself is skipped: load.c's pre-pass resolves it before any walk
void walk_section(const char* source, Section section, toml_datum_t tab, const Algorithm* algo);

// Prints the rows of `table` whose scope is exactly `scope`
void print_help_from_table(const FieldSpec* table, unsigned scope);

// Prints `name = value` for the rows of `table` a run can set, read through `base`
void dump_fields(const FieldSpec* table, const void* base);

// Writes the sections in `scope` as text, e.g. "[common] or [[run]]"
void scope_text(unsigned scope, char* buf, size_t size);
void set_defaults(const FieldSpec* table, void* base);

// --- Runner fields (config.c) ---

// Fields that belong to the runner rather than to any algorithm
extern const FieldSpec runner_fields[];

void validate(void);
void dump_config(void);
