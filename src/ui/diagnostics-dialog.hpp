#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace langlive::obsplugin::ui {

// Exports a diagnostics zip for LangLive support.
class DiagnosticsDialog : public QDialog {
	Q_OBJECT

public:
	explicit DiagnosticsDialog(QWidget *parent);

private:
	void OnChangeLocationClicked();
	void OnExportClicked();
	void ShowExported(const QString &zipPath);

	QString directory_;
	QLineEdit *directoryEdit_ = nullptr;
	QLabel *fileNameLabel_ = nullptr;
	QPushButton *changeButton_ = nullptr;
	QPushButton *cancelButton_ = nullptr;
	QPushButton *exportButton_ = nullptr;
};

// Shown after a successful export: where the file is, and a way to open
// its folder.
class DiagnosticsExportedDialog : public QDialog {
	Q_OBJECT

public:
	DiagnosticsExportedDialog(const QString &zipPath, QWidget *parent);
};

} // namespace langlive::obsplugin::ui
