#pragma once

#include "unittest.h"  // IWYU pragma: export

// Loads `toml` as a config file: written to a temporary file, loaded with load_batch, removed
Batch load_toml(const char* toml);

// Where the algorithm field `key` is stored in a loaded run's algorithm config, found through the
// algorithm's field table, so tests read it without the config struct private to its file
void* algo_field(const RunConfig* rc, const char* key);

// The same for an algorithm config `base` laid out by `algo`, e.g. one filled by set_defaults
void* algo_field_in(const Algorithm* algo, void* base, const char* key);
