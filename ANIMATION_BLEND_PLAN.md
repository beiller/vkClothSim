# Animation blending plan (`AnimationPlayer.strength`)

Status: **implemented and verified** (see Verification results at the bottom). Companion to
the animation-timing investigation (fixed 240 Hz tick + render interpolation verified
correct; the walk/run loop glitch in `alien-grey-rigged.glb` is an asset issue — last
keyframe ≠ first, up to ~10° on some joints).

## Goal

Fade animations in/out and **blend between clips** (primary use: crossfade walk ↔ run in
the character controller) by driving a per-player `strength` float (default 1.0) that is
itself animatable via the URL system — settable from Lua, keyframe-able with
`scene.animate`, declarable in scene JSON.

## Why this plugs in cleanly (verified)

The field plumbing is fully reflection-driven (`src/core/reflect.hpp`):

- `url.cpp` resolves URLs through the `FieldDesc` table → `scene.set("…AnimationPlayer.strength", 0.5)` works
- `SliderF` ⇒ `fieldBytes = 4` ⇒ animatable by clips in `animStepClip`
- `scene.animate(target, "AnimationPlayer.strength", keys)` builds a keyframed fade clip from Lua
- scene JSON parsing (`writeField`) and Lua `compEmplace` are reflection-driven; missing key → default 1.0, so old scenes load unchanged
- no clamping anywhere in writes (slider lo/hi are display-only)
- single caller of `animStepClip`: `stepAnimations` (systems.cpp), which has the player in hand

So the field itself is a two-table addition: `AnimationPlayer.strength` in
`components.hpp` + one `FieldDesc` row in `kAnimationPlayer`.

## Design

### The field

```cpp
struct AnimationPlayer {
    entt::entity clip = entt::null;
    float time = 0.0f, speed = 1.0f;
    int loop = 1;
    bool playing = false;
    float strength = 1.0f;   // NEW: contribution weight / fade amount (see below)
};
// reflect.hpp: {"strength", OFF(AnimationPlayer, strength), FieldKind::SliderF, 0.0f, 1.0f}
```

### Blending is per-target, implicit — no group component

Animations target arbitrary URLs. We do **not** declare which clips blend (no
`LayeredAnimation` component). Instead: every tick, each track is already resolved to a
concrete entity + field (a pointer) before it writes. **Two tracks from different playing
players that resolve to the same pointer are, by definition, fighting over the same value**
— today they stomp each other in opaque entity-creation order; with this change they blend.

- The unit of blending is the **field**, not the clip: walk and run blend exactly on the
  fields they share (the 24 joints + Hips); a field only one clip touches gets a solo write.
- Partial overlap composes for free (a future one-joint "arm wave" overlay blends on that
  joint, other joints stay solo).
- Zero authoring/wiring: works with dynamically created players (`scene.animate`), no
  membership lists to keep stale.
- **Blast radius is zero**: anything where two playing clips write one field today is
  already order-dependent coin-flip behavior; nothing that works today changes.

Why not a `LayeredAnimation` group component: it solves a declaration problem that
per-target resolution makes unnecessary, and adds exactly the maintenance burden this
codebase makes painful (runtime-created players would silently fall outside groups). If
per-layer *policy* ever becomes a real need (additive overlays, priority/interrupt for
flinch/emote clips), the right extension is a small per-player mode field
(`replace`/`additive`/`exclusive`) — still no group component.

### The formula (single, continuous — no mode switch)

Per target, collect contributions `(sampledValue, weight)` from all playing players whose
tracks resolve to it:

```
no contributions            → don't write (pose holds)
otherwise                   → result = lerp( current, weightedMix(samples), clamp(Σw, 0, 1) )
                              write once
```

- `weightedMix` = normalized weights; quats via a slerp chain (pairwise, descending
  weight), vec3/float via linear accumulation.
- Crossfade (walk 0.5 + run 0.5): Σ=1 → exact 50/50 mix of the two samples — a true crossfade.
- Solo fade (one player at 0.3): pull current toward the sample by 0.3 — fade-in from rest,
  fade-out to "hold last pose" (matches existing idle semantics; does not snap to bind).
- Fade both out: Σ→0 → pose eases back to whatever is currently there.
- All zero: no write.
- "Current" at tick time is the target's live value (for joints: the previous frame's
  interpolated render pose) — same reference point the stomp behavior had, so fades are
  grounded in the actual displayed state.

### Implementation shape

`stepAnimations` becomes two-pass:

1. **Sample**: every playing player samples its tracks into a scratch entry
   `{targetPtr, kind, width, weight, sample[4]}` (reused buffer; today's code already
   allocates a `vector<Trk>` per clip per tick, so no new allocation class).
2. **Resolve**: group entries by target pointer (n is small — linear scan or sort), apply
   the formula, write each target once.

**Fast path**: when exactly one playing player exists and its strength ≥ 1.0 (or: every
target has a single full-strength contributor), take today's code path verbatim —
bit-identical output for all existing scenes, zero overhead.

`strength > 1`: the pull is clamped to [0, 1] (this field is a fade/weight amount;
out-of-range values are full/no-op rather than producing extrapolated poses). Weights in
multi mode are normalized, so relative ratios still work if someone exceeds 1.

## Semantics notes (agreed mental model)

- In multi-contributor mode `strength` is a **relative weight** (walk 0.5 + run 1.0 =
  33/67). Drive transitions so weights sum to ~1.
- A playing player at strength 0 still advances its clock → pre-roll before fading in.
- Paused players contribute nothing (unchanged).
- No priority/interrupt semantics: two full-strength clips on one field mix 50/50. If
  override-style overlays become a real need, add the per-player mode field then.

## Scene integration (`alien_ctrl.lua`) — done

- `setAnim` crossfades strengths over `FADE = 0.25 s` instead of hard-toggling `playing`:
  both players play while either has weight, weights cross linearly (sum stays 1 → true
  crossfade), the faded-out player is stopped at fade end so its clock no longer advances.
  A mid-fade state flip resumes from the current weights (no snap).
- **Phase matching**: when a clip resumes from a stopped clock, its start is mapped to the
  other clip's gait phase — `to.time = ((from.time / fromDur) % 1) * toDur` (crude cycle
  mapping; the clips have different keyframe counts, so this aligns stride fraction, not
  pose). Without it a stale clock would mix at a random relative phase.
- **Speed smoothing**: per-tick `r.speed` oscillates hard at 240 Hz (6DOF joint catch-up:
  0 / 2 / 4.5 / 9 m/s tick to tick), which flaps the idle/walk/run state every tick and
  restarts the fade forever. A low-pass filter (τ = 0.1 s) on the speed used for state
  selection fixes it; the raw value is untouched for `char_ctrl`'s own use.

## Verification results

All headless (`--no-graphics --no-interp`; `--no-interp` matters — with render
interpolation on, `interpolateBones` rewrites joint Transforms every frame, so pose reads
see the interpolated render value, not the tick value):

- **Blend math** (temp scene sampling walk@0.3 / run@0.2 on LeftUpLeg): both @0.5 → exact
  component midpoint of the two solo samples (vec3 within 2e-7; quat on the arc with equal
  half-angles). Solo @0.25 pulled from that pose → exact lerp/slerp at 0.25. All pass.
- **Crossfade envelope** (temp scene splicing the real `setAnim`/`tickFade` block, scripted
  walk→run→idle timeline): strengths cross linearly over exactly 60 ticks, sum = 1.000000
  throughout, playing flags correct (both on during fade, faded-out one stopped after).
  Deceleration naturally chains run→walk→idle as the smoothed speed decays.
- **Fast path bit-identity**: A/B dump of all 24 animated joints × 120 ticks, single
  full-strength player — old binary (stashed) vs new: byte-identical.
- **Regression**: windowed `--frames 2` ×15 clean + `--frames 60` clean on `alien_ctrl`.
