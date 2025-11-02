#include "audio/pipewire_controller.hpp"

#include "syzygy/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef SYZYGY_HAVE_PIPEWIRE
extern "C" {
#include <pipewire/main-loop.h>
#include <pipewire/pipewire.h>
#include <pipewire/loop.h>
#include <spa/buffer/buffer.h>
#include <spa/param/audio/format-utils.h>
}
#endif

namespace syzygy::audio {

namespace {

constexpr double kMaxBufferedSeconds = 1.0;
constexpr uint32_t kDefaultRate = 48000;
constexpr uint32_t kDefaultChannels = 2;

#ifdef SYZYGY_HAVE_PIPEWIRE

struct SourceFinder {
  SourceFinder(std::optional<std::string> bus_path,
               std::optional<std::string> label_hint)
      : bus(bus_path ? *bus_path : std::string{}) {
    if (label_hint && !label_hint->empty()) {
      hints.push_back(*label_hint);
      const auto pos = label_hint->find(':');
      if (pos != std::string::npos) {
        hints.push_back(label_hint->substr(0, pos));
      }
    }
  }
  std::string bus;
  std::vector<std::string> hints;
  std::optional<uint32_t> node_id;
};

bool contains_ci(std::string_view haystack, const std::string& needle) {
  if (needle.empty() || haystack.size() < needle.size()) {
    return false;
  }
  const auto to_lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  for (std::size_t i = 0; i <= haystack.size() - needle.size(); ++i) {
    bool match = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (to_lower(haystack[i + j]) != to_lower(needle[j])) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

void registry_global_cb(void* data, uint32_t id, uint32_t permissions,
                        const char* type, uint32_t version,
                        const spa_dict* props) {
  (void)permissions;
  (void)version;
  auto* finder = static_cast<SourceFinder*>(data);
  if (!finder || finder->node_id) {
    return;
  }
  if (!type || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) {
    return;
  }

  const char* media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (!media_class) {
    return;
  }
  const std::string_view media(media_class);
  if (media.find("Audio/Source") == std::string_view::npos) {
    return;
  }

  const char* bus_path = spa_dict_lookup(props, PW_KEY_DEVICE_BUS_PATH);
  if (!bus_path) {
    bus_path = spa_dict_lookup(props, PW_KEY_DEVICE_BUS);
  }
  if (!bus_path) {
    bus_path = spa_dict_lookup(props, PW_KEY_DEVICE_SERIAL);
  }
  if (bus_path && finder->bus == bus_path) {
    finder->node_id = id;
    return;
  }

  const char* description_c = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
  const std::string_view desc_sv =
      description_c ? std::string_view(description_c) : std::string_view{};
  const char* node_name_c = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  const std::string_view name_sv =
      node_name_c ? std::string_view(node_name_c) : std::string_view{};
  const char* device_desc_c = spa_dict_lookup(props, PW_KEY_DEVICE_DESCRIPTION);
  const std::string_view device_desc_sv =
      device_desc_c ? std::string_view(device_desc_c) : std::string_view{};

  for (const auto& hint : finder->hints) {
    if (hint.empty()) {
      continue;
    }
    if (!desc_sv.empty() && contains_ci(desc_sv, hint)) {
      finder->node_id = id;
      return;
    }
    if (!name_sv.empty() && contains_ci(name_sv, hint)) {
      finder->node_id = id;
      return;
    }
    if (!device_desc_sv.empty() && contains_ci(device_desc_sv, hint)) {
      finder->node_id = id;
      return;
    }
  }
}

static const pw_registry_events kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = registry_global_cb,
    .global_remove = nullptr,
};

std::optional<uint32_t> find_source_node(
    const std::optional<std::string>& bus_path,
    const std::optional<std::string>& label_hint) {
  if ((!bus_path || bus_path->empty()) &&
      (!label_hint || label_hint->empty())) {
    return std::nullopt;
  }

  pw_main_loop* loop = pw_main_loop_new(nullptr);
  if (!loop) {
    return std::nullopt;
  }

  pw_context* context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
  if (!context) {
    pw_main_loop_destroy(loop);
    return std::nullopt;
  }

  pw_core* core = pw_context_connect(context, nullptr, 0);
  if (!core) {
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
    return std::nullopt;
  }

  SourceFinder finder(bus_path, label_hint);
  spa_hook listener{};
  pw_registry* registry =
      pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener(registry, &listener, &kRegistryEvents, &finder);

  auto* loop_api = pw_main_loop_get_loop(loop);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
  while (!finder.node_id && std::chrono::steady_clock::now() < deadline) {
    pw_loop_iterate(loop_api, 50);
  }

  pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
  pw_core_disconnect(core);
  pw_context_destroy(context);
  pw_main_loop_destroy(loop);
  return finder.node_id;
}
#endif  // SYZYGY_HAVE_PIPEWIRE

}  // namespace

struct PipeWireController::Impl {
  explicit Impl(PipeWireController& outer_ref) : outer(outer_ref) {}

#ifdef SYZYGY_HAVE_PIPEWIRE
  PipeWireController& outer;
  pw_main_loop* loop{nullptr};
  pw_stream* capture_stream{nullptr};
  pw_stream* playback_stream{nullptr};
  std::thread thread;
  std::mutex fifo_mutex;
  std::deque<int16_t> fifo;
  std::size_t max_fifo_samples{0};
  std::atomic<bool> running{false};

  std::optional<uint32_t> node_id;
  std::optional<uint32_t> resolved_node_id;
  std::optional<std::string> bus_path;
  std::optional<std::string> description;
  uint32_t rate{kDefaultRate};
  uint32_t channels{kDefaultChannels};
  bool playback_ready{false};
  bool fallback_route{false};

  bool format_logged{false};
  bool capture_logged{false};
  std::chrono::steady_clock::time_point last_diff_log{};
  std::chrono::steady_clock::time_point last_resync{};

  void loop_body() {
    pw_loop* pwloop = pw_main_loop_get_loop(loop);
    while (running.load()) {
      pw_loop_iterate(pwloop, 10);
    }
  }

  void reset_fifo_locked() {
    fifo.clear();
    capture_logged = false;
  }

  void configure_from_format(const spa_audio_info_raw& info) {
    std::lock_guard<std::mutex> lock(fifo_mutex);
    if (info.rate > 0) {
      rate = info.rate;
    }
    if (info.channels > 0) {
      channels = info.channels;
    }
    max_fifo_samples =
        static_cast<std::size_t>(static_cast<double>(rate) *
                                 static_cast<double>(channels) *
                                 kMaxBufferedSeconds);
    if (max_fifo_samples == 0) {
      max_fifo_samples = static_cast<std::size_t>(rate * channels);
    }
    while (fifo.size() > max_fifo_samples) {
      fifo.pop_front();
    }
  }

  bool ensure_playback_stream(uint32_t desired_rate, uint32_t desired_channels) {
    if (playback_stream &&
        desired_rate == rate &&
        desired_channels == channels &&
        playback_ready) {
      return true;
    }

    if (playback_stream) {
      pw_stream_disconnect(playback_stream);
      pw_stream_destroy(playback_stream);
      playback_stream = nullptr;
      playback_ready = false;
    }

    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_S16;
    info.rate = desired_rate;
    info.channels = desired_channels;
    static constexpr spa_audio_channel stereo_positions[] = {
        SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR};
    for (uint32_t i = 0; i < info.channels && i < 2; ++i) {
      info.position[i] = stereo_positions[i];
    }

    pw_properties* playback_props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE, "Game", PW_KEY_APP_NAME, "syzygy", nullptr);

    playback_stream = pw_stream_new_simple(
        pw_main_loop_get_loop(loop), "syzygy-audio-playback",
        playback_props, &playback_events(), this);
    if (!playback_stream) {
      syzygy::log::warn("PipeWireController: failed to create playback stream");
      return false;
    }

    uint8_t buffer[256];
    spa_pod_builder builder;
    spa_pod_builder_init(&builder, buffer, sizeof(buffer));
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

    const pw_stream_flags flags = static_cast<pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS);

    if (pw_stream_connect(playback_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                          flags, params, 1) < 0) {
      syzygy::log::warn("PipeWireController: playback connect failed");
      pw_stream_destroy(playback_stream);
      playback_stream = nullptr;
      return false;
    }

    configure_from_format(info);
    playback_ready = true;
    syzygy::log::info("PipeWire playback stream ready",
                      "rate", rate, "channels", channels);
    return true;
  }

  bool create_capture_stream() {
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_S16;
    info.rate = rate;
    info.channels = channels;
    static constexpr spa_audio_channel stereo_positions[] = {
        SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR};
    for (uint32_t i = 0; i < info.channels && i < 2; ++i) {
      info.position[i] = stereo_positions[i];
    }

    pw_properties* capture_props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Game", PW_KEY_APP_NAME, "syzygy",
        nullptr);

    capture_stream = pw_stream_new_simple(
        pw_main_loop_get_loop(loop), "syzygy-audio-capture",
        capture_props, &capture_events(), this);
    if (!capture_stream) {
      syzygy::log::warn("PipeWireController: failed to create capture stream");
      return false;
    }

    uint8_t buffer[256];
    spa_pod_builder builder;
    spa_pod_builder_init(&builder, buffer, sizeof(buffer));
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

    const pw_stream_flags flags = static_cast<pw_stream_flags>(
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS);

    const uint32_t target = resolved_node_id ? *resolved_node_id : PW_ID_ANY;
    if (pw_stream_connect(capture_stream, PW_DIRECTION_INPUT, target,
                          flags, params, 1) < 0) {
      syzygy::log::warn("PipeWireController: capture connect failed");
      pw_stream_destroy(capture_stream);
      capture_stream = nullptr;
      return false;
    }

    return true;
  }

  void destroy_capture_stream() {
    if (capture_stream) {
      pw_stream_disconnect(capture_stream);
      pw_stream_destroy(capture_stream);
      capture_stream = nullptr;
    }
  }

  void destroy_playback_stream() {
    if (playback_stream) {
      pw_stream_disconnect(playback_stream);
      pw_stream_destroy(playback_stream);
      playback_stream = nullptr;
      playback_ready = false;
    }
  }

  static void on_capture_state_changed(void* data,
                                       pw_stream_state old_state,
                                       pw_stream_state state,
                                       const char* error) {
    auto* self = static_cast<Impl*>(data);
    syzygy::log::info("PipeWire capture stream",
                      pw_stream_state_as_string(old_state),
                      "->", pw_stream_state_as_string(state));
    if (error) {
      syzygy::log::warn("PipeWire capture error", error);
    }
    if (state == PW_STREAM_STATE_ERROR) {
      self->outer.stop();
    }
  }

  static void on_capture_param_changed(void* data, uint32_t id,
                                       const spa_pod* param) {
    auto* self = static_cast<Impl*>(data);
    if (!self || !param || id != SPA_PARAM_Format) {
      return;
    }
    spa_audio_info_raw info{};
    if (spa_format_audio_raw_parse(param, &info) < 0) {
      return;
    }
    self->configure_from_format(info);
    self->ensure_playback_stream(info.rate, info.channels);
    if (!self->format_logged) {
      syzygy::log::info("PipeWire capture format",
                        "rate", info.rate,
                        "channels", info.channels,
                        "format", info.format);
      self->format_logged = true;
    } else {
      syzygy::log::info("PipeWire capture format update",
                        "rate", info.rate,
                        "channels", info.channels,
                        "format", info.format);
    }
  }

  static void on_playback_state_changed(void* data,
                                        pw_stream_state old_state,
                                        pw_stream_state state,
                                        const char* error) {
    auto* self = static_cast<Impl*>(data);
    (void)self;
    syzygy::log::info("PipeWire playback stream",
                      pw_stream_state_as_string(old_state),
                      "->", pw_stream_state_as_string(state));
    if (error) {
      syzygy::log::warn("PipeWire playback error", error);
    }
  }

  static void on_playback_param_changed(void* data, uint32_t id,
                                        const spa_pod* param) {
    auto* self = static_cast<Impl*>(data);
    if (!self || !param || id != SPA_PARAM_Format) {
      return;
    }
    spa_audio_info_raw info{};
    if (spa_format_audio_raw_parse(param, &info) < 0) {
      return;
    }
    syzygy::log::info("PipeWire playback format",
                      "rate", info.rate,
                      "channels", info.channels,
                      "format", info.format);
  }

  static void on_capture_process(void* data) {
    auto* self = static_cast<Impl*>(data);
    pw_buffer* buffer = pw_stream_dequeue_buffer(self->capture_stream);
    if (!buffer) {
      syzygy::log::warn("PipeWire capture underrun");
      return;
    }

    spa_buffer* spa = buffer->buffer;
    auto* spa_data = &spa->datas[0];
    if (!spa_data || !spa_data->chunk || spa_data->chunk->size == 0) {
      pw_stream_queue_buffer(self->capture_stream, buffer);
      return;
    }

    auto* chunk = spa_data->chunk;
    auto* bytes = static_cast<uint8_t*>(spa_data->data) + chunk->offset;
    const std::size_t frame_size = sizeof(int16_t) * self->channels;
    std::size_t frames = chunk->size / frame_size;
    auto* samples = reinterpret_cast<int16_t*>(bytes);

    if (!self->capture_logged) {
      syzygy::log::info("PipeWire capture buffer",
                        "frames", frames,
                        "chunk_size", chunk->size);
      self->capture_logged = true;
    }

    float gain = self->outer.gain_;
    if (std::abs(gain - 1.0f) > 1e-3f) {
      for (std::size_t i = 0; i < frames * self->channels; ++i) {
        float value = static_cast<float>(samples[i]) * gain;
        value = std::clamp(value, -32768.0f, 32767.0f);
        samples[i] = static_cast<int16_t>(value);
      }
    }

    double sum_sq = 0.0;
    for (std::size_t i = 0; i < frames * self->channels; ++i) {
      const float value = static_cast<float>(samples[i]) / 32768.0f;
      sum_sq += value * value;
    }
    const double rms = std::sqrt(sum_sq / (frames * self->channels));
    self->outer.peak_level_.store(static_cast<float>(rms));

    {
      std::lock_guard<std::mutex> lock(self->fifo_mutex);
      const std::size_t samples_to_push = frames * self->channels;
      if (samples_to_push > 0) {
        for (std::size_t i = 0; i < samples_to_push; ++i) {
          self->fifo.push_back(samples[i]);
        }
        const std::size_t limit =
            std::max<std::size_t>(self->max_fifo_samples,
                                  samples_to_push * 4);
        while (self->fifo.size() > limit) {
          self->fifo.pop_front();
        }
      }
    }

    pw_stream_queue_buffer(self->capture_stream, buffer);
  }

  static void on_playback_process(void* data) {
    auto* self = static_cast<Impl*>(data);
    self->drain_to_playback();
  }

  static const pw_stream_events& capture_events() {
    static const pw_stream_events events = [] {
      pw_stream_events ev{};
      ev.version = PW_VERSION_STREAM_EVENTS;
      ev.state_changed = &Impl::on_capture_state_changed;
      ev.param_changed = &Impl::on_capture_param_changed;
      ev.process = &Impl::on_capture_process;
      return ev;
    }();
    return events;
  }

  static const pw_stream_events& playback_events() {
    static const pw_stream_events events = [] {
      pw_stream_events ev{};
      ev.version = PW_VERSION_STREAM_EVENTS;
      ev.state_changed = &Impl::on_playback_state_changed;
      ev.param_changed = &Impl::on_playback_param_changed;
      ev.process = &Impl::on_playback_process;
      return ev;
    }();
    return events;
  }

  void drain_to_playback() {
    if (!playback_stream) {
      return;
    }

    pw_buffer* buffer = pw_stream_dequeue_buffer(playback_stream);
    if (!buffer) {
      return;
    }

    spa_buffer* spa = buffer->buffer;
    auto* spa_data = &spa->datas[0];
    if (!spa_data || !spa_data->data) {
      pw_stream_queue_buffer(playback_stream, buffer);
      return;
    }

    auto* chunk = spa_data->chunk;
    const std::size_t frame_size = sizeof(int16_t) * channels;
    std::size_t frames = 0;
    if (chunk && chunk->size > 0) {
      frames = chunk->size / frame_size;
    } else if (spa_data->maxsize > 0) {
      frames = spa_data->maxsize / frame_size;
    }
    if (frames == 0) {
      frames = 128;  // fall back to a nominal size
    }

    if (frames == 0) {
      pw_stream_queue_buffer(playback_stream, buffer);
      return;
    }

    auto* out = reinterpret_cast<int16_t*>(
        static_cast<uint8_t*>(spa_data->data) +
        (chunk ? chunk->offset : 0));
    const std::size_t samples_needed = frames * channels;
    std::size_t copied = 0;

    {
      std::lock_guard<std::mutex> lock(fifo_mutex);
      while (copied < samples_needed && !fifo.empty()) {
        out[copied++] = fifo.front();
        fifo.pop_front();
      }
    }

    if (copied < samples_needed) {
      std::fill(out + copied, out + samples_needed, 0);
    }

    if (chunk) {
      chunk->offset = 0;
      chunk->stride = static_cast<int32_t>(frame_size);
      chunk->size = static_cast<uint32_t>(samples_needed * sizeof(int16_t));
    }

    pw_stream_queue_buffer(playback_stream, buffer);
  }
#endif  // SYZYGY_HAVE_PIPEWIRE
};

PipeWireController::PipeWireController() {
#ifdef SYZYGY_HAVE_PIPEWIRE
  static std::once_flag init_flag;
  std::call_once(init_flag, []() { pw_init(nullptr, nullptr); });
  impl_ = new Impl(*this);
#else
  impl_ = nullptr;
#endif
}

PipeWireController::~PipeWireController() {
  stop();
#ifdef SYZYGY_HAVE_PIPEWIRE
  delete impl_;
#endif
}

bool PipeWireController::start(std::optional<uint32_t> node_id,
                               std::optional<std::string> bus_path,
                               std::optional<std::string> description,
                               uint32_t channels,
                               uint32_t rate) {
#ifdef SYZYGY_HAVE_PIPEWIRE
  if (!impl_) {
    return false;
  }

  stop();

  impl_->node_id = node_id;
  impl_->bus_path = bus_path;
  impl_->description = description;
  impl_->channels = channels;
  impl_->rate = rate;
  impl_->resolved_node_id.reset();
  impl_->format_logged = false;
  impl_->capture_logged = false;
  impl_->fallback_route = false;
  impl_->fifo.clear();
  impl_->max_fifo_samples =
      static_cast<std::size_t>(static_cast<double>(rate) *
                               static_cast<double>(channels) *
                               kMaxBufferedSeconds);

  std::optional<uint32_t> resolved = node_id;
  if (!resolved) {
    resolved = find_source_node(bus_path, description);
    if (resolved) {
      syzygy::log::info("PipeWire: matched source node", *resolved,
                        bus_path.value_or(description.value_or("unknown")));
    } else {
      impl_->fallback_route = true;
      syzygy::log::warn("PipeWire: falling back to default route for",
                        bus_path.value_or(description.value_or("unknown")));
    }
  }
  impl_->resolved_node_id = resolved;

  impl_->loop = pw_main_loop_new(nullptr);
  if (!impl_->loop) {
    syzygy::log::warn("PipeWireController: failed to create main loop");
    return false;
  }

  if (!impl_->ensure_playback_stream(rate, channels)) {
    syzygy::log::warn("PipeWireController: playback unavailable");
  }

  if (!impl_->create_capture_stream()) {
    pw_main_loop_destroy(impl_->loop);
    impl_->loop = nullptr;
    return false;
  }

  impl_->running = true;
  impl_->thread = std::thread([this]() { impl_->loop_body(); });
  return true;
#else
  (void)node_id;
  (void)bus_path;
  (void)description;
  (void)channels;
  (void)rate;
  syzygy::log::warn("PipeWireController: PipeWire support not compiled in");
  return false;
#endif
}

void PipeWireController::stop() {
#ifdef SYZYGY_HAVE_PIPEWIRE
  if (!impl_) {
    return;
  }
  if (impl_->running.exchange(false)) {
    if (impl_->loop) {
      pw_main_loop_quit(impl_->loop);
    }
    if (impl_->thread.joinable()) {
      impl_->thread.join();
    }
  }
  impl_->destroy_capture_stream();
  impl_->destroy_playback_stream();
  if (impl_->loop) {
    pw_main_loop_destroy(impl_->loop);
    impl_->loop = nullptr;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->fifo_mutex);
    impl_->fifo.clear();
  }
  peak_level_.store(0.0f);
#endif
}

void PipeWireController::set_gain(float gain) {
  gain_ = gain;
}

bool PipeWireController::is_running() const noexcept {
#ifdef SYZYGY_HAVE_PIPEWIRE
  return impl_ && impl_->capture_stream != nullptr;
#else
  return false;
#endif
}

uint32_t PipeWireController::sample_rate() const noexcept {
#ifdef SYZYGY_HAVE_PIPEWIRE
  return impl_ ? impl_->rate : kDefaultRate;
#else
  return kDefaultRate;
#endif
}

uint32_t PipeWireController::channels() const noexcept {
#ifdef SYZYGY_HAVE_PIPEWIRE
  return impl_ ? impl_->channels : kDefaultChannels;
#else
  return kDefaultChannels;
#endif
}

}  // namespace syzygy::audio
