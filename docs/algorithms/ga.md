# GA - Genetic Algorithm

A population of individuals, each a string of bits that encodes a point in the search space. Every generation, parents are selected by their fitness, paired, recombined by crossover and mutated, and the children replace the population.

optimiCe implements a binary, generational GA (Holland, 1975; Goldberg, 1989) with four selection operators, two crossover operators, and optional family-competition elitism. The run's result is the best individual of the final generation.

## How it works

### Encoding

Each individual carries `ind_size` bits per dimension, `n_dims × ind_size` bits in all. A dimension's bits are read as an unsigned integer, its *level*, from 0 to $2^{L} - 1$ with $L$ = `ind_size`, and the levels are spread evenly over the domain: level 0 decodes to `domain_min` and the highest level to `domain_max`. The GA therefore searches a grid of $2^{L}$ values per dimension, and the domain must be finite.

For a dimension at level $\ell$:

$$x = \text{domain\_min} + \ell \, \frac{\text{domain\_max} - \text{domain\_min}}{2^{L} - 1}$$

Within a dimension, bits are numbered from the least significant, bit 0, to the most significant, bit $L - 1$. The numbering matters for `cut_points`.

### Initialization

When the init range is the whole domain, every bit of every individual is drawn at random. When the init range is narrower, random bits would land anywhere in the domain, so each dimension instead draws one of the levels that decode inside the init range, uniformly, and takes that level's bits. Every individual is then evaluated.

### Each generation

1. **Selection.** `pop_size` parents are drawn from the population, with repetition, by the `selection` operator. Parents keep the fitness they already have, so selection costs no evaluations.
2. **Crossover.** The selected parents are paired in order, first with second, third with fourth, and so on, and every pair is crossed by the `crossover` operator; there is no crossover probability. When `pop_size` is odd, the last individual has no partner and is only mutated.
3. **Mutation.** Every bit of every child flips with probability `mutation_rate`.
4. **Evaluation.** Every child is evaluated once, whether or not crossover and mutation changed it.
5. **Replacement.** The children replace the whole population; under `elitism = "family"` they first compete with their parents.

The early stop looks at the best individual of each generation. Without elitism, a generation's best can be lost in the next one, so the final generation's best, which is the run's result, may be worse than an individual seen earlier.

## Options

### Selection

Three operators build a weight for every individual, larger for better ones, and then spin a roulette wheel `pop_size` times: each spin picks an individual with probability proportional to its weight.

- `roulette` (the default): fitness-proportional. Weights are shifted so the worst individual gets weight 1. The shift is 1 fitness unit, so the selection pressure depends on the scale of the fitness values: selection is close to uniform when the fitness values differ by much less than 1, and strongly biased toward the best when they differ by much more.

  $$w_i = (f_\text{max} - f_i) + 1 \;\text{ when minimizing}, \qquad w_i = (f_i - f_\text{min}) + 1 \;\text{ when maximizing}$$

- `roulette_normalized`: like `roulette`, but the shift past the worst individual is the population's average fitness spacing instead of 1 fitness unit. The pressure no longer depends on the fitness scale, and the worst individual keeps a small chance of being picked. When every individual has the same fitness, all weights are equal. With $n$ = `pop_size` and $\Delta = f_\text{max} - f_\text{min}$, and $d_i$ the distance from the worst fitness ($f_\text{max} - f_i$ when minimizing, $f_i - f_\text{min}$ when maximizing):

  $$w_i = \frac{d_i + \Delta / (n - 1)}{\Delta}$$

- `rank`: linear ranking (Baker, 1985). Individuals are weighted by their rank alone, so the fitness values only matter through their order. `rank_pressure` sets the expected number of copies of the best individual; the worst gets `2 − rank_pressure`, and `1.0` selects uniformly. With $s$ = `rank_pressure` and ranks $r$ from 0 for the worst to $n - 1$ for the best:

  $$w_r = (2 - s) + 2 (s - 1) \, \frac{r}{n - 1}$$

The fourth operator uses no weights:

- `tournament`: each parent is the best of `tournament_size` individuals drawn at random, with repetition. Larger tournaments mean stronger pressure.

### Crossover

- `n_point` (the default): dimension-aware n-point crossover. `cut_points` lists cut ratios, and the cuts apply within each dimension's bits independently: a ratio $c$ cuts the dimension at bit $\lfloor L \cdot c \rfloor$. The segments between cuts alternate between kept and swapped, starting with kept, from bit 0 up. `[0.6]` is classic single-point crossover, and `[0.3, 0.7]` is two-point. Since bits are numbered from the least significant, a single cut swaps the most significant part of every dimension: with `ind_size = 22`, `[0.6]` cuts at bit 13 and swaps bits 13 to 21.
- `uniform`: every bit is swapped between the two children independently, with probability 0.5 (Syswerda, 1989).

### Mutation

Every bit of every child flips independently with probability `mutation_rate`. The expected number of flips per individual is `mutation_rate × n_dims × ind_size`.

### Elitism

- `none` (the default): the children replace their parents unconditionally.
- `family`: elitist recombination (Thierens & Goldberg, 1994). After crossover and mutation, each pair keeps the best two of its two parents and its two children. Children win ties, which lets the search drift across plateaus instead of freezing on the parents. An unpaired last individual keeps the better of itself and its mutant, and the mutant wins a tie. It costs no extra evaluations: every child is evaluated once either way, and the parents' fitness is already known.

A family's survivors are never worse than its parents, but family elitism only protects individuals that were selected: a good individual that selection skips can still be lost.

## Fields

<!-- gen:fields ga -->
<!-- Generated by `make docs` from the source tables; edits here are overwritten -->

| Key | Type | Default | Values | Description |
|-----|------|---------|--------|-------------|
| `pop_size` | int | `15` |  | Population size (at least 2) |
| `ind_size` | int | `22` |  | Bits per dimension, in [2, 63] |
| `tournament_size` | int | `2` |  | Individuals per tournament (tournament selection) |
| `rank_pressure` | float | `1.5` |  | Expected copies of the best under rank selection, in [1.0, 2.0] |
| `cut_points` | float[] | `[0.6]` |  | Cut ratios for n_point crossover, in (0.0, 1.0), increasing |
| `mutation_rate` | float | `0.01` |  | Per-bit flip probability, in [0.0, 1.0) |
| `selection` | enum | `roulette` | `roulette`, `roulette_normalized`, `rank`, `tournament` | Parent selection operator |
| `crossover` | enum | `n_point` | `n_point`, `uniform` | Crossover operator |
| `elitism` | enum | `none` | `none`, `family` | Elitism scheme |

<!-- /gen -->

Checked at startup:

- `pop_size` must be at least 2.
- `ind_size` must be in [2, 63].
- The domain must be finite at both ends.
- The init range must contain at least one level: with few bits, a narrow init range can fall between two neighbouring levels.
- `mutation_rate` must be in [0.0, 1.0).
- With `selection = "tournament"`, `tournament_size` must be between 2 and `pop_size`.
- With `selection = "rank"`, `rank_pressure` must be in [1.0, 2.0].
- With `crossover = "n_point"`, `cut_points` must hold at least one ratio, each in (0.0, 1.0) and strictly increasing, and every cut must actually split the bits: a ratio that lands on bit 0, or two ratios that land on the same bit, are rejected, since they'd silently give fewer cuts than listed.

Fields an operator doesn't use are ignored and not checked: `tournament_size` outside tournament selection, `rank_pressure` outside rank selection, `cut_points` outside n-point crossover.

## Example

Tournament selection, two-point crossover and family elitism, on 10-dimensional Rastrigin:

```toml
[common]
fitness    = "rastrigin"
n_dims     = 10
domain_min = -5.12
domain_max = 5.12
iterations = 500
replicates = 30

[[run]]
algorithm       = "ga"
pop_size        = 100
ind_size        = 16
selection       = "tournament"
tournament_size = 3
crossover       = "n_point"
cut_points      = [0.3, 0.7]
mutation_rate   = 0.01
elitism         = "family"
```

## References

- J. H. Holland, *Adaptation in Natural and Artificial Systems*, University of Michigan Press, 1975.
- D. E. Goldberg, *Genetic Algorithms in Search, Optimization, and Machine Learning*, Addison-Wesley, 1989.
- J. E. Baker, "Adaptive selection methods for genetic algorithms," *Proceedings of the First International Conference on Genetic Algorithms and Their Applications*, pp. 101–111, 1985.
- G. Syswerda, "Uniform crossover in genetic algorithms," *Proceedings of the Third International Conference on Genetic Algorithms*, pp. 2–9, 1989.
- D. Thierens and D. E. Goldberg, "Elitist recombination: an integrated selection recombination GA," *Proceedings of the First IEEE Conference on Evolutionary Computation, IEEE World Congress on Computational Intelligence*, 1994.
