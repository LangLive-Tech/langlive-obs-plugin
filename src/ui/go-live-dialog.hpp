#pragma once

#include "app/ports.hpp"

#include <QDialog>

#include <functional>
#include <QImage>

class QComboBox;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace langlive::obsplugin {
class PluginController;
struct StartLiveResult;
} // namespace langlive::obsplugin

namespace langlive::obsplugin::ui {

class CoverPicker;

// Tells the streamer why StartLive did not start: missing fields or the
// backend's refusal. Pre-stream check violations are the caller's.
void ShowStartLiveFailure(QWidget *parent, const StartLiveResult &result);

// Live room settings and start / end live. Modeless: it stays open while
// streaming and follows the controller's live state. Closing it (X or
// Escape) only hides it, so the form is kept until logout.
class GoLiveDialog : public QDialog {
	Q_OBJECT

public:
	GoLiveDialog(PluginController &controller, QWidget *parent);

	// Re-reads login and live state from the controller.
	void Refresh();

	// The live did not start (the pre-stream check stopped it): stay open.
	void CancelPendingClose();
	// The live was started again (the pre-stream check's Apply and go
	// live): close once it is live.
	void CloseWhenLive() { closeWhenLive_ = true; }

	// Loads channels, events, and the values saved on the backend. The
	// first successful load fills the title, push notification, and
	// channel; later ones refresh the lists and keep what the streamer
	// chose.
	void ReloadInfo();

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	QWidget *CreateRoomSection();
	QWidget *CreateStreamSection();
	void LoadSavedSettings();

	void ApplyInfo(const GoLiveInfo &info);
	// Downloads and shows the cover at `url` (GoLiveInfo::coverUrl).
	void ShowCover(const std::string &url);
	// Footer buttons follow the live state and pending backend calls.
	void UpdateButtons();

	// Upload Cover: pick an image, crop it, upload it (in the crop dialog).
	void OnUploadCoverClicked();
	// The crop dialog's Upload Cover: uploads `cover` and makes it the cover
	// if the backend took it.
	void UploadCover(const QImage &cover, std::function<void(bool, QString)> done);
	void OnSaveClicked();
	void OnStartEndClicked();

	GoLiveRequest CollectRequest();

	PluginController &controller_;

	QLabel *liveNotice_ = nullptr;
	QWidget *loadingNotice_ = nullptr; // spinner and text while streaming/info is loading
	QWidget *form_ = nullptr;          // everything above the footer; disabled while live
	CoverPicker *coverPicker_ = nullptr;
	QPushButton *uploadButton_ = nullptr;
	QLineEdit *titleEdit_ = nullptr;
	QLineEdit *pushEdit_ = nullptr;
	QComboBox *mainEventCombo_ = nullptr;
	QComboBox *subEventCombo_ = nullptr;
	QComboBox *channelCombo_ = nullptr;
	QLabel *orientationHint_ = nullptr;
	QRadioButton *generalRadio_ = nullptr;
	QRadioButton *highQualityRadio_ = nullptr;
	QPushButton *saveButton_ = nullptr;
	QPushButton *startEndButton_ = nullptr;

	// On macOS the Escape that closes the native file picker also reaches
	// this dialog; ignore Escape while the picker is open and just after.
	bool ignoreEscape_ = false;
	bool closeWhenLive_ = false; // Start Live clicked; close when the live starts
	bool infoLoaded_ = false;    // the backend's values have been applied once
	bool loadingInfo_ = false;
	bool saving_ = false;
	std::string shownCoverUrl_; // the cover shown (or being downloaded), empty if none
};

} // namespace langlive::obsplugin::ui
