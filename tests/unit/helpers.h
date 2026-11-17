#pragma once

#include "unittest.h"  // IWYU pragma: export

// Loads `toml` as a config file: written to a temporary file, loaded with load_batch, removed
Batch load_toml(const char* toml);

// Where the algorithm field `key` is stored in a loaded run's algorithm config, found through the
// algorithm's field table, so tests read it without the config struct private to its file
void* algo_field(const RunConfig* rc, const char* key);

// The same for an algorithm config `base` laid out by `algo`, e.g. one filled by set_defaults
void* algo_field_in(const Algorithm* algo, void* base, const char* key);

// A registered algorithm by name; fails the test when there's none
const Algorithm* algo_named(const char* name);

// Where the field `key` of the algorithm `name` is stored in this thread's live config, the one
// its functions read. Set the algorithm's defaults first, with set_defaults
void* live_field(const char* name, const char* key);

// Runs job j of `batch` as the runner does; the result's position is allocated, free it after
RunResult run_job_of(const Batch* batch, int j);
