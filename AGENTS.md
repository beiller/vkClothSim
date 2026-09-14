# 3dsim

DO NOT ALTER THIS FILE via coding agent

A Jolt physics simulation, with a GPU soft-body simulation bolted on top that runs on the
GPU and uses capsules as colliders. The soft bodies (the cloth + the ball) both run on the
GPU. The compute shader is passed just two things per body — its **vertices** and its
**joints** (two-point distance constraints) — plus the Jolt capsules (as colliders). The
capsules from Jolt are passed to the sim, which runs on the GPU.

Use separation of concerns
- Rendering
- Cloth / Soft Body Sim
- Jolt physics loop

Inter-communication between those channels.

The code should be architechted so there is minimal coupling between the pieces listed, they may one day, but not today, be refactored into separate threads and have queued up commands.

Move the code in this direction given the opportunity. 

Minimize the use of comments and remove them when you see them unless absolutely required, which is almost never.

Use discriptive variables names and function names.

Keep all output concise: minimize text and avoid long summaries or logs.

## Build & run

```sh
cmake -S . -B build -Wno-dev        # re-run after editing any shader (SPIR-V is baked here)
cmake --build build -j
./build/vksim                        # the app
./build/vksim --shot s.ppm [--steps N]   # settle N steps, render one frame, write PPM, exit
```


# Desired Code State
## Verlet Integration

Core Verlet approach (the "simple but works" baseline)

Representation: cloth as a grid of point masses (positions + previous positions, no explicit velocity — that's the Verlet trick). Store as a big buffer/texture: position, prevPosition per particle.
Integration step (per particle, fully parallel):
newPos = pos + (pos - prevPos) * damping + acceleration * dt²
This is "Position Verlet" — velocity is implicit in the position delta, which makes it trivially parallelizable and very stable.
Constraint satisfaction (the hard part on GPU): each particle is connected to neighbors (structural, shear, bend springs) via distance constraints. You resolve these by iterating: for each constraint, push the two particles apart/together to satisfy the target distance.
Problem: neighboring particles read/write each other's positions — a race condition if done naively in parallel.
Solution: graph coloring or checkerboard/Jacobi-style updates. Classic trick: split constraints into independent sets (e.g., all "red" edges, then all "black" edges) so no two constraints in the same pass touch the same particle. Run several Jacobi/Gauss-Seidel-like iteration passes (8–20 typically) per frame for stiffness.
Collision handling: sphere/plane/SDF collisions resolved as extra position corrections, self-collision optionally via spatial hashing on GPU.
Normals + render: recompute normals from the position buffer, feed into vertex shader.

This is basically the NVIDIA "GPU Gems"-style / PositionBasedDynamics (PBD) approach — and this is important: modern state-of-the-art-but-simple is not really "Verlet with distance constraints" anymore, it's Position Based Dynamics (PBD) or XPBD.

What's considered current best-practice-but-still-simple: XPBD

XPBD (Extended Position Based Dynamics, Müller et al. 2016) is the natural evolution: same Verlet integration backbone, but constraints are solved with a compliance parameter so stiffness is independent of iteration count and substep count — this fixes PBD's classic problem where stiffness changes if you change the solver iteration count or timestep.
Typical modern pipeline:
Verlet/semi-implicit integration for predicted positions
Multiple substeps (not just constraint iterations) — substepping is the modern trick that replaced "many constraint iterations" because it's more stable and physically consistent
Gauss-Seidel or Jacobi constraint solve with compliance (alpha) terms, run on GPU via graph-colored constraint groups
Velocity update derived from position deltas after solving
This is what you'll see in Unity/Unreal cloth-ish demos, Nvidia Flex/PhysX cloth, and most modern GPU cloth research since ~2017.

## Graph coloring and GPU scheduling:
Your constraint graph (edges between particles) usually isn't as clean as a regular checkerboard, so it's really graph coloring in general: assign each constraint a color such that constraints sharing a particle never share a color. For a cloth grid with structural + shear + bend springs, you typically need more than 2 colors (often 4–8) since each particle can be touched by many constraints. 

Why this matters for GPU specifically

Each color group becomes one dispatch (one compute shader invocation over that group's constraints).
Within a dispatch, every thread can safely read and write particle positions with no atomics needed, because the coloring guarantees no two threads in that dispatch touch the same particle.
You pay a synchronization cost between color groups (each color = a separate dispatch + memory barrier), but you gain full parallelism within each group.

So concretely for your XPBD cloth: at setup time (once, on CPU, since your mesh topology is fixed), you'd build the constraint list and greedily assign each constraint a color, then group constraint indices by color into separate buffers. At runtime, you dispatch one compute pass per color, each pass safely updating all its particles in parallel.