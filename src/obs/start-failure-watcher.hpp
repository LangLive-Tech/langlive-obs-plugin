#pragma once

#include <QObject>

#include <functional>

namespace langlive::obsplugin::obs {

// Reports when OBS gives up starting a stream before it is running. OBS
// sends no frontend event for this: when setting up the stream or starting
// the output fails (e.g. the encoder cannot start), it only emits its main
// window's StreamingStopped Qt signal and shows its own error. The same
// signal also follows a normal stop; the callback is meant for
// PluginController::OnStreamStartFailed, which ignores it unless a start
// requested by the plugin is pending.
//
// The callback runs where OBS emits the signal (the main thread), before
// OBS shows its error dialog.
class StartFailureWatcher final : public QObject {
	Q_OBJECT

public:
	// `mainWindow`: OBS's main window, which declares the signal.
	StartFailureWatcher(QObject *mainWindow, std::function<void()> callback);

	// False if this OBS version has no such signal (nothing is reported).
	bool IsWatching() const { return watching_; }

private slots:
	void OnStreamingStopped();

private:
	std::function<void()> callback_;
	bool watching_ = false;
};

} // namespace langlive::obsplugin::obs
