#include "syzygy/clock.hpp"
#include "syzygy/log.hpp"
#include "syzygy/scope_timer.hpp"

// Copyright (c) 2025 Zoe Gates <zoe@zeocities.dev>

#include <pipewire/main-loop.h>
#include <pipewire/pipewire.h>
#include <pipewire/loop.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/param.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct CaptureApp {
  pw_main_loop* loop = nullptr;
  pw_stream* stream = nullptr;
  float gain = 1.0f;
  uint64_t total_frames = 0;
  syzygy::clock::TimePoint start_time{};
  uint32_t channels = 2;
  uint32_t rate = 48000;
  std::optional<std::string> target_node;
};

void on_stream_state_changed(void* userdata,
                             enum pw_stream_state old,
                             enum pw_stream_state state,
                             const char* error_message) {
  syzygy::log::info("Stream state changed",
                    pw_stream_state_as_string(old),
                    "->", pw_stream_state_as_string(state));
  if (error_message && *error_message) {
    syzygy::log::warn("  Error:", error_message);
  }
}

void apply_gain(int16_t* samples,
                size_t num_frames,
                uint32_t channels,
                float gain) {
  if (std::abs(gain - 1.0f) < 1e-3f) {
    return;
  }

  for (size_t i = 0; i < num_frames * channels; ++i) {
    float value = static_cast<float>(samples[i]) * gain;
    value = std::clamp(value, -32768.0f, 32767.0f);
    samples[i] = static_cast<int16_t>(value);
  }
}

void on_process(void* userdata) {
  auto* app = static_cast<CaptureApp*>(userdata);
  pw_buffer* buffer = pw_stream_dequeue_buffer(app->stream);
  if (!buffer) {
    syzygy::log::warn("Stream underrun detected (no buffer)");
    return;
  }

  spa_buffer* spa = buffer->buffer;
  auto* data = &spa->datas[0];

  if (!data->chunk || data->chunk->size == 0) {
    pw_stream_queue_buffer(app->stream, buffer);
    return;
  }

  const auto size = data->chunk->size;
  auto* bytes = static_cast<uint8_t*>(data->data) + data->chunk->offset;
  auto* samples = reinterpret_cast<int16_t*>(bytes);

  const size_t frame_size = sizeof(int16_t) * app->channels;
  const size_t frames = size / frame_size;

  apply_gain(samples, frames, app->channels, app->gain);

  app->total_frames += frames;
  const double elapsed_ms = syzygy::clock::milliseconds_since(app->start_time);
  const double accumulated_ms =
      static_cast<double>(app->total_frames) / static_cast<double>(app->rate) *
      1000.0;

  syzygy::log::info("Captured", frames, "frames (", size, "bytes).",
                    "Elapsed real:", std::fixed, std::setprecision(2),
                    elapsed_ms, "ms",
                    "Audio timeline:", accumulated_ms, "ms");

  pw_stream_queue_buffer(app->stream, buffer);
}

pw_stream_events make_stream_events() {
  pw_stream_events events{};
  events.version = PW_VERSION_STREAM_EVENTS;
  events.destroy = nullptr;
  events.state_changed = on_stream_state_changed;
  events.control_info = nullptr;
  events.io_changed = nullptr;
  events.param_changed = nullptr;
  events.add_buffer = nullptr;
  events.remove_buffer = nullptr;
  events.process = on_process;
  events.drained = nullptr;
  events.command = nullptr;
  events.trigger_done = nullptr;
  return events;
}

void parse_arguments(int argc, char** argv, CaptureApp& app) {
  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];
    if (arg == "--gain" && i + 1 < argc) {
      app.gain = std::strtof(argv[++i], nullptr);
    } else if (arg == "--channels" && i + 1 < argc) {
      app.channels = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (arg == "--rate" && i + 1 < argc) {
      app.rate = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (arg == "--node" && i + 1 < argc) {
      app.target_node = argv[++i];
    } else if (arg == "--help") {
      std::cout << "Usage: audio_probe [--gain N] [--channels C] [--rate R]"
                   " [--node ID]\n";
      std::exit(0);
    }
  }

  syzygy::log::info("Configured gain:", app.gain,
                    "channels:", app.channels,
                    "rate:", app.rate,
                    "node:", app.target_node.value_or("default"));
}

spa_audio_info_raw make_audio_info(const CaptureApp& app) {
  spa_audio_info_raw info{};
  info.format = SPA_AUDIO_FORMAT_S16;
  info.rate = app.rate;
  info.channels = app.channels;

  static constexpr spa_audio_channel stereo_positions[] = {
      SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR};
  for (uint32_t i = 0; i < info.channels && i < 2; ++i) {
    info.position[i] = stereo_positions[i];
  }
  return info;
}

pw_stream* create_stream(CaptureApp& app) {
  const std::string latency_hint =
      "128/" + std::to_string(app.rate);

  pw_properties* props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "Game",
      PW_KEY_APP_NAME, "syzygy-audio-probe",
      PW_KEY_NODE_LATENCY, latency_hint.c_str(),
      nullptr);

  if (app.target_node) {
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, app.target_node->c_str());
  }

  static const pw_stream_events events = make_stream_events();

  pw_stream* stream = pw_stream_new_simple(
      pw_main_loop_get_loop(app.loop),
      "Syzygy Audio Capture",
      props,
      &events,
      &app);

  if (!stream) {
    syzygy::log::fatal("Failed to create PipeWire stream");
  }

  return stream;
}

void connect_stream(CaptureApp& app) {
  const auto info = make_audio_info(app);

  uint8_t buffer[1024];
  spa_pod_builder builder;
  spa_pod_builder_init(&builder, buffer, sizeof(buffer));

  const spa_pod* params[1];
  params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

  const pw_stream_flags flags = static_cast<pw_stream_flags>(
      PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
      PW_STREAM_FLAG_RT_PROCESS);

  int target_id = PW_ID_ANY;
  if (app.target_node) {
    try {
      target_id = std::stoi(*app.target_node);
    } catch (const std::exception& e) {
      syzygy::log::warn("Unable to parse node id", *app.target_node,
                        "- falling back to autoconnect.");
      target_id = PW_ID_ANY;
    }
  }

  int res = pw_stream_connect(
      app.stream,
      PW_DIRECTION_INPUT,
      target_id,
      flags,
      params,
      1);

  if (res < 0) {
    syzygy::log::fatal("Failed to connect stream, PipeWire error");
  }
}

}  // namespace

int main(int argc, char** argv) {
  syzygy::profiling::ScopeTimer timer{"PipeWire audio capture prototype"};

  pw_init(&argc, &argv);

  CaptureApp app{};
  parse_arguments(argc, argv, app);

  app.loop = pw_main_loop_new(nullptr);
  if (!app.loop) {
    syzygy::log::fatal("Failed to create PipeWire main loop");
  }

  app.stream = create_stream(app);
  app.start_time = syzygy::clock::now();

  connect_stream(app);

  // Run the loop for a fixed window to gather baseline measurements.
  pw_loop* loop = pw_main_loop_get_loop(app.loop);
  for (int i = 0; i < 50; ++i) {
    pw_loop_iterate(loop, 200);
  }

  pw_stream_disconnect(app.stream);
  pw_stream_destroy(app.stream);
  pw_main_loop_destroy(app.loop);
  pw_deinit();

  syzygy::log::info("Total frames captured:", app.total_frames);
  return 0;
}
