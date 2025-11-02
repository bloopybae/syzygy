# Measurement Logs

Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

This directory collects raw outputs from Phase 0 prototypes.

- `audio_48k_hd60s.log` — User-provided PipeWire capture against the Elgato HD60 S+ at 48 kHz/128 frames.
- `audio_48k.log` — Partial attempt to run the probe within the sandbox (fails due to PipeWire access restrictions).
- `hdr_probe.log` — HDR probe output; metadata capture requires elevated permissions to read `/sys/kernel/debug/dri/0/hdmi_infoframe`.

Re-run probes on the development workstation and append new logs here before advancing to Phase 1.

