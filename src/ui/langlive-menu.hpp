#pragma once

#include "app/plugin-controller.hpp"
#include "langlive/obsplugin/core/validation.hpp"

#include <QObject>
#include <QPointer>

#include <functional>
#include <vector>

class QAction;
class QMainWindow;
class QMenu;

namespace langlive::obsplugin {
class PluginController;
}

namespace langlive::obsplugin::ui {

class GoLiveDialog;
class UpdateDialog;

// The top-level LangLive menu in OBS's menu bar, and the dialogs opened
// from it.
class LangLiveMenu : public QObject {
	Q_OBJECT

public:
	// QA builds name the menu 浪LIVE(Test), so QA and production builds
	// cannot be mistaken for each other.
	LangLiveMenu(PluginController &controller, QMainWindow *mainWindow, bool qaBuild = false);
	~LangLiveMenu() override;

	// Updates menu items and open dialogs after a controller state change.
	void Refresh();

	// Called when the pre-stream check stopped a stream.
	void ShowPreflightFailure(std::vector<core::Violation> violations);

	// The backend ended the login (expired token, or the account may no
	// longer stream from OBS); the plugin has logged out.
	void ShowLoginLost(ApiError::Kind reason);

	// A plugin live ended because the stream was stopped outside the plugin
	// (e.g. OBS's Stop Streaming). A notice with OK only: it cannot be
	// undone.
	void ShowLiveEndedOutside();

private:
	void OnLoginTriggered();
	void OnGoLiveTriggered();
	void OnDiagnosticsTriggered();
	void OnHelpTriggered();
	void OnCheckForUpdatesTriggered();

	// What a menu item does while an update is required.
	enum class WhenRequired {
		Block,          // log in, go live: only the update dialog
		RunAfterDialog, // the rest: the update dialog, then the item once it is closed
	};
	// Every menu item but Check for Updates checks for updates first,
	// silently: an optional update is shown only by Check for Updates. A
	// required one is shown, and `action` runs as `whenRequired` says;
	// otherwise `action` runs at once. A failed check leaves a required
	// update required.
	void CheckThen(WhenRequired whenRequired, std::function<void()> action);
	void OpenGoLive();
	// The dialog shown (an open one is brought to the front).
	UpdateDialog *ShowUpdateDialog(const UpdateCheck &check);

	PluginController &controller_;
	QMainWindow *mainWindow_;
	QAction *loginAction_ = nullptr;
	QAction *goLiveAction_ = nullptr;
	QPointer<GoLiveDialog> goLiveDialog_;
	QPointer<UpdateDialog> updateDialog_;
	UpdateCheck lastUpdateCheck_;
	bool checkingForUpdates_ = false;
};

} // namespace langlive::obsplugin::ui
