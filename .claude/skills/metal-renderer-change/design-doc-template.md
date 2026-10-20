# <Change name>

**Hypothesis:** <metric> on <scene> at <pose and quality> moves from <baseline> to <target> on <phone>.
**Base commit:** <sha>
**Floor:** <unchanged, or the raised GPU family / OS / MSL version and why>

## Grounding

One row per API, MSL feature or limit the change uses or relies on.

| Item | Source and line | Available from | Floor verdict |
|---|---|---|---|
| | SDK header / DocumentationSearch / MSL section / FST row | family, iOS, MSL | within floor / runtime check + fallback / raises floor |

## Splat data contract

- Cloud layout: which buffers the change reads or writes, their element type and stride.
- Sort keys: width, encoding, direction.
- Visibility and LOD selection: what is culled or selected, where, and what count reaches the draw.
- Per world versus per frame: which buffers are written once per world, which per frame slot.
- Budget: memory added or removed, and the splat count it holds at.

## Resources

For each buffer or texture the change creates, resizes or retires: owner, storage mode, size formula, created when, released when, frame slots it spans.

## Data flow

Per pass in GPU order: inputs, outputs, dispatch or draw shape (threads per threadgroup, grid, indirect source), function constants, bindings.

## Synchronization

- Command buffer and encoder boundaries, and which pass waits on which.
- Barriers or fences, and the hazard each one covers.
- CPU readbacks and the frame slot each one waits on.
- The numeric test that proves each dependency, since validation does not report a missing barrier.

## Tests

Each Mac GPU test the change adds or changes, with the CPU reference it compares against.

## Measurement

Scene, pose, quality and run count for ios-perf; the metric fields that decide the hypothesis.

## Files

Allow-list for the refactor step.

## Results

Filled after step 7: both sides' figures, spread, verdict.
