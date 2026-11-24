#pragma once

#include "app/plugin-controller.hpp"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace langlive::obsplugin::ui {

// Offers a newer version: release notes, then download, verify, and hand
// over to the installer. A required update has no "Later" but can be closed
// (PM decision): login and go-live then check again and show it again.
class UpdateDialog : public QDialog {
	Q_OBJECT

public:
	UpdateDialog(PluginController &controller, const UpdateCheck &check, QWidget *parent);

	// Closing during the download cancels it.
	void reject() override;

signals:
	// The installer is starting; OBS must quit so it can replace the plugin.
	void QuitRequested();

private:
	void OnUpdateClicked();
	void SetBusy(bool busy);

	PluginController &controller_;
	bool required_ = false;
	bool downloading_ = false;
	QProgressBar *progress_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *laterButton_ = nullptr;
	QPushButton *updateButton_ = nullptr;
};

} // namespace langlive::obsplugin::ui
