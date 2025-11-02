# Audio Prototype

Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

`audio_probe` establishes a low-latency PipeWire capture stream, applies software gain, and logs real-time vs. audio timeline clocks to evaluate drift.

```bash
cmake -S prototypes -B build/prototypes -G Ninja
ninja -C build/prototypes audio_probe
./build/prototypes/audio/audio_probe --gain 1.2 --channels 2 --rate 48000
```

Use `--node <id>` to bind to a specific PipeWire node (see `pw-cli ls Node`). After the 10-second capture window completes, the tool reports the total frames captured as a quick sanity check.

