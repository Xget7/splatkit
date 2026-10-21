# <Change name>

**Hypothesis:** <metric> on <scene> at <pose and quality> moves from <baseline> to <target> on <iPhone> and from <baseline> to <target> on <Android phone>.
**Base commit:** <sha>
**Floor:** <unchanged on both, or the raised GPU family / OS / shading language / Vulkan version and why>

## Backend comparison

The stage as each backend runs it today, and every difference.

| Difference | Metal (file:line) | Vulkan (file:line) | Port gap in this run, or kept on purpose and why |
|---|---|---|---|
| | | | |

## Grounding

One row per API, shading-language feature or limit the change uses or relies on, on either backend.

| Backend | Item | Source and line | Available from | Floor verdict |
|---|---|---|---|---|
| Metal | | SDK header / DocumentationSearch / MSL section / FST row | family, iOS, MSL | within floor / runtime check + fallback / raises floor |
| Vulkan | | `vulkan_core.h` / spec section / reference phone capabilities | core version or extension, Android | within floor / runtime check + fallback / raises floor |

## Splat data contract

Shared by both backends; a field that differs between them is a row in the comparison above.

- Cloud layout: which buffers the change reads or writes, their element type and stride.
- Sort keys: width, encoding, direction.
- Visibility and LOD selection: what is culled or selected, where, and what count reaches the draw.
- Per world versus per frame: which buffers are written once per world, which per frame slot.
- Budget: memory added or removed, and the splat count it holds at.

## Resources

Per backend, for each buffer or texture the change creates, resizes or retires: owner, memory or storage mode, size formula, created when, released when, frame slots it spans.

## Data flow

Per backend, per pass in GPU order: inputs, outputs, dispatch or draw shape (threads or invocations per group, grid, indirect source), specialization or function constants, bindings.

## Synchronization

Per backend:

- Command buffer and encoder or render pass boundaries, and which pass waits on which.
- Barriers, fences or semaphores, and the hazard each one covers.
- CPU readbacks and the frame slot each one waits on.
- The numeric test that proves each dependency, since no validation layer reports every missing barrier.

## Tests

Each GPU test the change adds or changes, with the CPU reference it compares against; a shared behaviour has the same case on both backends.

| Case | Metal test | Vulkan test |
|---|---|---|
| | | |

## Measurement

Scene, pose, quality and run count per phone (ios-perf, android-perf); the metric fields that decide the hypothesis on each.

## Files

Allow-list for the refactor step, per backend.

## Results

Filled after the measure step: each backend's figures on both sides, spread, verdict.
