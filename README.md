# Syzygy

Syzygy is a low-latency PipeWire preview app for HDMI capture cards on Linux. It gives you a native GTK interface with live audio gain, capture stats, and fullscreen playback that stays in sync with the card.

## Building

Dependencies (Arch package names):

- `gtkmm-4.0`
- `libpipewire-0.3`
- `libudev`
- `libv4l2`
- `gstreamer-1.0`
- A C++20 compiler and `ninja`

```bash
cmake -S . -B build -G Ninja
ninja -C build syzygy_app
./build/src/syzygy_app
```

## License

MIT © 2025 Zoe Gates <zoe@zeocities.dev>
