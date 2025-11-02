# Video Prototype

Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

`video_probe` inventories V4L2 capture devices, enumerates pixel formats and frame sizes, verifies DV timings, and confirms DMA-BUF export capability.

```bash
cmake -S prototypes -B build/prototypes -G Ninja
ninja -C build/prototypes video_probe
./build/prototypes/video/video_probe
```

Pair the output with the Vulkan DMA-BUF import spike (next binary) to validate the full capture-to-present loop.

