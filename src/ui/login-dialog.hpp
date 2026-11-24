#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace langlive::obsplugin {
class PluginController;
}

namespace langlive::obsplugin::ui {

class QrCodeWidget;

// Login with a phone number and an SMS code, or by scanning a QR code with
// the LangLive app. Mock until the LangLive login API is available.
class LoginDialog : public QDialog {
	Q_OBJECT

public:
	LoginDialog(PluginController &controller, QWidget *parent);

private:
	QWidget *CreatePhonePage();
	QWidget *CreateQrPage();
	void ShowPage(int index);
	// Disables the phone page's buttons while the backend answers.
	void SetWaiting(bool waiting);
	// "Send code" needs a complete phone number and no countdown; "Log in"
	// a 6-digit code for the number a code was sent to. Neither while
	// waiting.
	void UpdatePhoneButtons();
	// Under the phone number: its format is wrong (while typing), or it is
	// required (left empty).
	void UpdatePhoneHint();
	// The number as the backend gets it (country code and national number).
	std::string Cellphone() const;

	void OnSendCodeClicked();
	void OnResendTick();
	void OnPhoneLoginClicked();
	void ShowPhoneError(const QString &message);
	// Like the website: a field left empty shows "Required" under it, with a
	// red border, until something is typed in it.
	void WatchRequired(QLineEdit *edit, QLabel *required);
	static void MarkRequired(QLineEdit *edit, QLabel *required, bool missing);

	void StartQrLogin();
	void OnQrTick();
	void CheckQrLogin();
	// Stops the QR code login: covers the code with `message` and a refresh
	// button.
	void EndQrLogin(const QString &message = QString());

	PluginController &controller_;

	QPushButton *phoneTab_ = nullptr;
	QPushButton *qrTab_ = nullptr;
	QStackedWidget *pages_ = nullptr;

	QComboBox *countryCode_ = nullptr;
	QLineEdit *phoneEdit_ = nullptr;
	QLineEdit *codeEdit_ = nullptr;
	QPushButton *sendCodeButton_ = nullptr;
	QPushButton *submitButton_ = nullptr;
	bool waiting_ = false;   // a request to the backend is in progress
	std::string codeSentTo_; // Cellphone() a code was last sent to; empty if none
	QLabel *phoneRequired_ = nullptr;
	QLabel *codeRequired_ = nullptr;
	QLabel *codeSentHint_ = nullptr;
	QLabel *phoneError_ = nullptr;
	QTimer *resendTimer_ = nullptr;
	int resendSecondsLeft_ = 0;

	QrCodeWidget *qrCode_ = nullptr;
	QLabel *qrCountdown_ = nullptr;
	QTimer *qrTimer_ = nullptr;
	int qrSecondsLeft_ = 0;
	bool qrStarted_ = false;
	bool qrActive_ = false;   // a code is shown and polled
	bool qrChecking_ = false; // a qrcode/verify is in progress
	bool qrScanned_ = false;  // the app scanned it; the countdown makes way for a hint
};

} // namespace langlive::obsplugin::ui
