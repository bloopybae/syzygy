# Phase 0 Prototype Workspace

Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

This directory hosts self-contained experiments that validate the Phase 0 objectives. Each prototype should be executable directly on the development machine with the attached capture card.

## Structure

- `video/` — V4L2 capture + DMA-BUF export into Vulkan/OpenGL for latency measurement.
- `audio/` — PipeWire/ALSA capture loop with gain control and timestamp logging.
- `hdr/` — Tools for probing HDR metadata and experimenting with tone mapping shaders.
- `common/` — Shared C++ utilities (logging, high-resolution timers, error handling).

Create a minimal `main.cpp` in each subsection; keep dependencies local so we can iterate quickly.

## Build Guidance

We will use CMake (C++20) with Ninja. Suggested invocation once sources land:

```bash
cmake -S prototypes -B build/prototypes -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build/prototypes
```

Enable sanitizers for debugging:

```bash
cmake -S prototypes -B build/prototypes-asan -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DSYZYGY_ENABLE_ASAN=ON -DSYZYGY_ENABLE_TSAN=OFF
```

## Contribution Notes

- Prefer small, focused prototypes that demonstrate one concept (e.g., DMA-BUF import) before combining them.
- Document findings in `docs/phase0-report.md` and link to the prototype commit.
- Keep third-party dependencies vendored or submoduled only after vetting license compatibility.

