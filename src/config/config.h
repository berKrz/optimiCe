#pragma once

#include <tomlc17.h>

#include "algorithms/algorithm.h"

// --- Field engine (fields.c) ---

// Sets every key of the TOML table `tab`, in file order: a runner field goes into g_cfg, a field
// of `algo` into its cfg. `algo` is NULL for [common]
// Dies on an unknown key and on a key whose scope excludes `section`
// The `algorithm` key is skipped: load_batch resolves it before any walk
void walk_section(const char* source, Section section, toml_datum_t tab, const Algorithm* algo);

// Prints the rows of `table` whose scope is exactly `scope`
void print_help_from_table(const FieldSpec* table, unsigned scope);

// Prints `name = value` for the rows of `table` a run can set, read through `base`
// [common]-only rows (seed, replicates) are batch-wide and skipped: dump_batch prints them
void dump_fields(const FieldSpec* table, const void* base);

// Writes the sections in `scope` as text, e.g. "[common] or [[run]]"
void scope_text(unsigned scope, char* buf, size_t size);

// Writes every row's default through `base`
void set_defaults(const FieldSpec* table, void* base);

// --- Runner fields (config.c) ---

// The fields every run has, whatever its algorithm
extern const FieldSpec runner_fields[];

// Checks the run loaded into g_cfg, then its algorithm's cfg; dies on the first problem
// Resolves an unset init range to the domain
void validate(void);

// Prints the run loaded into g_cfg and its algorithm's cfg
void dump_config(void);
