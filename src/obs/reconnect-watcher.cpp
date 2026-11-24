#include "obs/reconnect-watcher.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <plugin-support.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

namespace langlive::obsplugin::obs {

namespace {

// How long a reconnected stream must stay up before it counts as back.
constexpr int kStableMs = 10000;

} // namespace

ReconnectWatcher::ReconnectWatcher(std::function<void(bool reconnecting)> callback) : callback_(std::move(callback)) {}

ReconnectWatcher::~ReconnectWatcher()
{
	Stop();
	alive_.reset();
}

void ReconnectWatcher::Start()
{
	Stop();
	output_ = obs_frontend_get_streaming_output(); // adds a reference, released in Stop()
	if (!output_)
		return;
	signal_handler_t *handler = obs_output_get_signal_handler(output_);
	signal_handler_connect(handler, "reconnect", &ReconnectWatcher::OnReconnect, this);
	signal_handler_connect(handler, "reconnect_success", &ReconnectWatcher::OnReconnectSuccess, this);
}

void ReconnectWatcher::Stop()
{
	if (stableTimer_)
		stableTimer_->stop();
	if (!output_)
		return;
	signal_handler_t *handler = obs_output_get_signal_handler(output_);
	signal_handler_disconnect(handler, "reconnect", &ReconnectWatcher::OnReconnect, this);
	signal_handler_disconnect(handler, "reconnect_success", &ReconnectWatcher::OnReconnectSuccess, this);
	obs_output_release(output_);
	output_ = nullptr;
}

void ReconnectWatcher::OnReconnect(void *data, calldata_t *params)
{
	obs_log(LOG_INFO, "stream disconnected; OBS retries in %lld s",
		static_cast<long long>(calldata_int(params, "timeout_sec")));
	static_cast<ReconnectWatcher *>(data)->Post(true);
}

void ReconnectWatcher::OnReconnectSuccess(void *data, calldata_t *)
{
	obs_log(LOG_INFO, "stream reconnected");
	static_cast<ReconnectWatcher *>(data)->Post(false);
}

void ReconnectWatcher::Post(bool reconnecting)
{
	std::weak_ptr<bool> alive = alive_;
	QMetaObject::invokeMethod(
		QCoreApplication::instance(),
		[alive, this, reconnecting]() {
			if (alive.lock())
				Handle(reconnecting);
		},
		Qt::QueuedConnection);
}

void ReconnectWatcher::Handle(bool reconnecting)
{
	if (!stableTimer_) {
		stableTimer_ = std::make_unique<QTimer>();
		stableTimer_->setSingleShot(true);
		stableTimer_->setInterval(kStableMs);
		QObject::connect(stableTimer_.get(), &QTimer::timeout, [this]() {
			obs_log(LOG_INFO, "stream stable again after reconnecting");
			callback_(false);
		});
	}

	if (reconnecting) {
		stableTimer_->stop(); // dropped again before it was stable
		callback_(true);
	} else {
		stableTimer_->start();
	}
}

} // namespace langlive::obsplugin::obs
