#pragma once

#include <functional>
#include <memory>

struct obs_output;
typedef struct obs_output obs_output_t;
struct calldata;
typedef struct calldata calldata_t;

class QTimer;

namespace langlive::obsplugin::obs {

// Reports when OBS loses the stream connection and starts retrying, and when
// the stream is back. OBS has no frontend event for this, only the stream
// output's "reconnect" / "reconnect_success" signals, which fire on the
// output's thread; the callback runs on the Qt main thread.
//
// "reconnect_success" only means the RTMP connection is up again; a server
// that rejects the stream accepts the connection and drops it a second
// later, over and over. So the stream counts as back only after the
// connection has stayed up for a while; until then it stays reconnecting.
class ReconnectWatcher {
public:
	explicit ReconnectWatcher(std::function<void(bool reconnecting)> callback);
	~ReconnectWatcher();

	ReconnectWatcher(const ReconnectWatcher &) = delete;
	ReconnectWatcher &operator=(const ReconnectWatcher &) = delete;

	// Watch the current stream output. Call on STREAMING_STARTED.
	void Start();
	// Stop watching. Call on STREAMING_STOPPED and before unloading.
	void Stop();

private:
	static void OnReconnect(void *data, calldata_t *params);
	static void OnReconnectSuccess(void *data, calldata_t *params);
	void Post(bool reconnecting);
	void Handle(bool reconnecting); // main thread

	std::function<void(bool)> callback_;
	obs_output_t *output_ = nullptr;
	std::unique_ptr<QTimer> stableTimer_; // main thread
	// Queued callbacks check this, so none runs after the watcher is gone.
	std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace langlive::obsplugin::obs
