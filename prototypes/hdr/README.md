# HDR Prototype Notes

Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

`hdr_probe` extracts HDR metadata from the GPU's HDMI infoframe to confirm that the capture pipeline receives EOTF and colorimetry descriptors. Run as root if debugfs requires elevated access:

```bash
sudo ./hdr_probe /sys/kernel/debug/dri/0/hdmi_infoframe
```

The accompanying `tonemap_aces.glsl` shader implements a baseline ACES-inspired tone mapping curve suitable for SDR fallback displays. Integrate it into the Vulkan video prototype once DMA-BUF import is working to evaluate quality and performance.

