#include "obs/start-failure-watcher.hpp"

#include <QMetaObject>

#include <utility>

namespace langlive::obsplugin::obs {

StartFailureWatcher::StartFailureWatcher(QObject *mainWindow, std::function<void()> callback)
	: callback_(std::move(callback))
{
	if (!mainWindow)
		return;
	// OBS 31 declares StreamingStopped(bool withDelay = false); also accept
	// a version without the argument. Checked first, so a missing signal
	// is not a Qt warning.
	const QMetaObject *meta = mainWindow->metaObject();
	const char *signal = nullptr;
	if (meta->indexOfSignal("StreamingStopped(bool)") >= 0)
		signal = SIGNAL(StreamingStopped(bool));
	else if (meta->indexOfSignal("StreamingStopped()") >= 0)
		signal = SIGNAL(StreamingStopped());
	if (signal)
		watching_ = static_cast<bool>(
			connect(mainWindow, signal, this, SLOT(OnStreamingStopped()), Qt::DirectConnection));
}

void StartFailureWatcher::OnStreamingStopped()
{
	if (callback_)
		callback_();
}

} // namespace langlive::obsplugin::obs
