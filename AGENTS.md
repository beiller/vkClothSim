# 3dsim

DO NOT ALTER THIS FILE via coding agent

A Jolt physics simulation, with a GPU soft-body simulation bolted on top that runs on the
GPU and uses capsules as colliders. The soft bodies (the cloth + the ball) both run on the
GPU. The compute shader is passed just two things per body — its **vertices** and its
**joints** (two-point distance constraints) — plus the Jolt capsules (as colliders). The
capsules from Jolt are passed to the sim, which runs on the GPU.

Use separation of concerns
- Rendering
- Cloth Sim
- Jolt physics loop

Inter-communication between those channels.

The code should be architechted so there is minimal coupling between the pieces listed, they may one day, but not today, be refactored into separate threads and have queued up commands.

Move the code in this direction given the opportunity. 

## Build & run

```sh
cmake -S . -B build -Wno-dev        # re-run after editing any shader (SPIR-V is baked here)
cmake --build build -j
./build/vksim                        # the app
./build/vksim --shot s.ppm [--steps N]   # settle N steps, render one frame, write PPM, exit
./build/simtest 120                  # headless CPU soft-body test
```
