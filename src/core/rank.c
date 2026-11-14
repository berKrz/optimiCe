#include <stdlib.h>

#include "algorithms/algorithm.h"

// Equal fitness goes by index, lowest first. The comparators therefore never call two different
// entries equal, so the result is the same whatever sorting algorithm qsort uses
static int by_index(const Ranked* a, const Ranked* b) {
  return (a->index > b->index) - (a->index < b->index);
}

static int best_first(const void* pa, const void* pb) {
  const Ranked* a = pa;
  const Ranked* b = pb;
  if (a->fitness != b->fitness) return is_better(a->fitness, b->fitness) ? -1 : 1;
  return by_index(a, b);
}

static int worst_first(const void* pa, const void* pb) {
  const Ranked* a = pa;
  const Ranked* b = pb;
  if (a->fitness != b->fitness) return is_better(a->fitness, b->fitness) ? 1 : -1;
  return by_index(a, b);
}

void rank_by_fitness(const double* fits, int n, Ranked* order, RankOrder direction) {
  for (int i = 0; i < n; i++) order[i] = (Ranked) {fits[i], i};
  qsort(order, (size_t) n, sizeof(Ranked), direction == RANK_BEST_FIRST ? best_first : worst_first);
}
