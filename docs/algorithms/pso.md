# PSO - Particle Swarm Optimization

A swarm of particles moves through the search space. Each particle remembers the best position it has visited (its *pbest*), and the swarm shares the best position any particle has visited (the *gbest*). At every iteration each particle changes its velocity, pulled toward both of those positions, and moves.

optimiCe implements the global-best PSO with an inertia weight (Shi & Eberhart, 1998), with an optional linear inertia schedule, velocity capping, and a choice between asynchronous and synchronous gbest updates. The run's result is the gbest: the best position any particle visited.

## How it works

### Initialization

Every particle starts at a position drawn uniformly from the init range, independently in each dimension, with zero velocity. Its starting position is evaluated, and its pbest is set according to `pbest_init` (see [pbest initialization](#pbest-initialization)). The gbest is the best of the particles' pbests.

### Each iteration

Particles are updated one at a time, in order. For each particle, every dimension gets a new velocity made of three parts:

- **Inertia:** the particle keeps a share `w` of its current velocity.
- **Cognitive pull:** a step toward its own pbest, scaled by `c1` and a random factor.
- **Social pull:** a step toward the gbest, scaled by `c2` and a random factor.

The two random factors are drawn uniformly from [0, 1), fresh for every particle and every dimension. When `v_max` is set, the new velocity is capped; then the particle moves by its velocity.

A coordinate that left the domain is brought back according to `bounds` (see [Configuration](../configuration.md)), which also acts on that dimension's velocity: `clamp` and `random` set it to 0, and `reflect` reverses it whenever the coordinate bounced an odd number of times, so the particle keeps moving away from the bound it hit.

The particle's new position is then evaluated. If it's better than its pbest, it becomes the new pbest; a tie keeps the old one. When the gbest is refreshed depends on `gbest_update`.

For particle $i$ and dimension $d$, with pbest $p_i$ and gbest $g$:

$$v_{i,d} \leftarrow w \, v_{i,d} + c_1 r_1 \, (p_{i,d} - x_{i,d}) + c_2 r_2 \, (g_d - x_{i,d}), \qquad r_1, r_2 \sim U[0, 1)$$

$$x_{i,d} \leftarrow x_{i,d} + v_{i,d}$$

## Options

### Inertia schedule

`w` is the share of its velocity a particle keeps from one iteration to the next. Large values keep particles moving and exploring; small values slow them down so they settle around the best positions found.

With `w_end` set, the inertia changes linearly from `w` at the first iteration to `w_end` at the last. A decreasing schedule moves the swarm from exploration toward exploitation over the run: `w = 0.9` decaying to `w_end = 0.4` is the classic setting (Shi & Eberhart, 1999). `w_end` above `w` gives an increasing schedule instead, which some studies found converges faster (Zheng et al., 2003). Leaving `w_end` unset keeps the inertia constant at `w`.

The schedule follows the run's budget (see [Stopping](../configuration.md#stopping)), not the iterations actually run: a run that stops early never reaches `w_end`. It's measured from the first iteration, so the first iteration always uses exactly `w`. Under an iteration budget, with $T$ = `iterations`, iteration $t$ (counting from 0) uses

$$w_t = w + (w_\text{end} - w) \, \frac{t}{T - 1}$$

reaching exactly `w_end` on the last iteration, and a budget of a single iteration uses `w`. Under an evaluation budget, with $E$ the evaluations used when an iteration starts, $E_0$ those of setup, and $E_\text{max}$ = `evaluations`, the iteration uses

$$w_E = w + (w_\text{end} - w) \, \frac{E - E_0}{E_\text{max} - E_0}$$

so the inertia approaches `w_end`, falling short of it by one iteration's share of the budget.

### Velocity cap

With `v_max` set, each velocity component is capped to the range [−cap, cap], where the cap is `v_max` times the domain's width, `domain_max − domain_min`. When either end of the domain is infinite, the domain has no width, and the init range's width is used instead. `v_max = 0`, the default, disables the cap.

The cap is applied after the velocity update and before the move. Without it, and with `w` near 1, velocities can grow without limit: the defaults (`w = 1`, `c1 = c2 = 2`, no cap) reproduce the original update of Kennedy & Eberhart (1995), which relied on a velocity cap to stay stable. For real use, set `w` below 1, decay it with `w_end`, or set `v_max`.

### gbest update

- `async` (the default): the gbest moves as soon as any particle's pbest beats it, so particles later in the same iteration are already pulled toward the new best.
- `sync`: every particle in the iteration is pulled toward the gbest as it stood at the start of the iteration, and the gbest is refreshed once, after the whole swarm has moved.

`sync` makes the order of the particles irrelevant within an iteration; `async` spreads good news faster.

### pbest initialization

- `position` (the default): a particle's pbest starts at its starting position.
- `random`: a particle's pbest starts at a second, independent draw from the init range. That draw is evaluated, so it costs one extra fitness evaluation per particle, which an evaluation budget counts.

## Fields

<!-- gen:fields pso -->
<!-- Generated by `make docs` from the source tables; edits here are overwritten -->

| Key | Type | Default | Values | Description |
|-----|------|---------|--------|-------------|
| `swarm_size` | int | `30` |  | Number of particles |
| `w` | float | `1.0` |  | Inertia weight (at the first iteration, when w_end is set) |
| `w_end` | float | (unset) |  | Inertia at the last iteration, changing linearly from w (unset = constant) |
| `v_max` | float | `0.0` |  | Velocity cap as a fraction of the domain width (0 = off) |
| `c1` | float | `2.0` |  | Cognitive coefficient: pull toward the particle's own best |
| `c2` | float | `2.0` |  | Social coefficient: pull toward the swarm's best |
| `pbest_init` | enum | `position` | `position`, `random` | Where each particle's best starts |
| `gbest_update` | enum | `async` | `async`, `sync` | When the swarm's best is refreshed |
| `bounds` | enum | `clamp` | `clamp`, `reflect`, `random` | Out-of-domain handling (ignored when the domain has no finite end) |

<!-- /gen -->

Checked at startup:

- `swarm_size` must be positive.
- `w`, `w_end`, `v_max`, `c1` and `c2` must be non-negative.
- `bounds = "random"` needs a domain that is finite, or infinite at both ends.

## Example

Decaying inertia with a velocity cap, on 30-dimensional Rastrigin:

```toml
[common]
fitness    = "rastrigin"
n_dims     = 30
domain_min = -5.12
domain_max = 5.12
iterations = 1000
replicates = 30

[[run]]
algorithm  = "pso"
swarm_size = 40
w          = 0.9
w_end      = 0.4
v_max      = 0.5   # half the domain's width: 5.12 per iteration
```

## References

- J. Kennedy and R. Eberhart, "Particle swarm optimization," *Proceedings of ICNN'95, International Conference on Neural Networks*, vol. 4, pp. 1942–1948, 1995. [doi:10.1109/ICNN.1995.488968](https://doi.org/10.1109/ICNN.1995.488968)
- Y. Shi and R. Eberhart, "A modified particle swarm optimizer," *Proceedings of the IEEE International Conference on Evolutionary Computation*, pp. 69–73, 1998. [doi:10.1109/ICEC.1998.699146](https://doi.org/10.1109/ICEC.1998.699146)
- Y. Shi and R. C. Eberhart, "Empirical study of particle swarm optimization," *Proceedings of the 1999 Congress on Evolutionary Computation*, pp. 1945–1950, 1999. [doi:10.1109/CEC.1999.785511](https://doi.org/10.1109/CEC.1999.785511)
- Y. Zheng, L. Ma, L. Zhang and J. Qian, "Empirical study of particle swarm optimizer with an increasing inertia weight," *Proceedings of the 2003 Congress on Evolutionary Computation*, pp. 221–226, 2003.
