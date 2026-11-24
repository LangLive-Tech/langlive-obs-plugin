#include "ui_test_support.hpp"

#include "ui/diagnostics-dialog.hpp"
#include "ui/go-live-dialog.hpp"
#include "ui/langlive-menu.hpp"
#include "ui/login-dialog.hpp"
#include "ui/message-dialog.hpp"
#include "ui/preflight-dialog.hpp"
#include "ui/update-dialog.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFocusEvent>
#include <QPointer>
#include <QThread>
#include <QValidator>
#include <QComboBox>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QRadioButton>

using namespace langlive::obsplugin;
using namespace langlive::obsplugin::uitest;
using core::Violation;
using ui::Text;

// --- Login -----------------------------------------------------------------------

namespace {

// The phone page's fields: the phone number, then the code.
void FillPhoneLogin(ui::LoginDialog &dialog, const QString &phone, const QString &code)
{
	const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
	REQUIRE(edits.size() >= 2);
	edits[0]->setText(phone);
	edits[1]->setText(code);
}

// Sends a code to `phone`, then types `code`: what logging in takes.
void SendCodeAndFill(ui::LoginDialog &dialog, const QString &phone, const QString &code)
{
	FillPhoneLogin(dialog, phone, QString());
	QPushButton *send = FindButton(dialog, Text("Login.SendCode"));
	REQUIRE(send);
	send->click();
	FillPhoneLogin(dialog, phone, code);
}

} // namespace

TEST_CASE_FIXTURE(ControllerFixture, "login: terms and privacy links follow the website and language")
{
	controller.SetWebsite("https://www.example.com", "ja-JP");
	ui::LoginDialog dialog(controller, nullptr);
	QString links;
	for (QLabel *label : dialog.findChildren<QLabel *>())
		links += label->text();
	CHECK(links.contains(QStringLiteral("https://www.example.com/ja-JP/legal/terms")));
	CHECK(links.contains(QStringLiteral("https://www.example.com/ja-JP/legal/privacy")));
}

TEST_CASE_FIXTURE(ControllerFixture, "login: switching tabs keeps the dialog size")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	ProcessEvents();
	const QSize phoneSize = dialog.size();

	FindButton(dialog, Text("Login.TabQr"))->click();
	ProcessEvents();
	CHECK(dialog.size() == phoneSize);

	FindButton(dialog, Text("Login.TabPhone"))->click();
	ProcessEvents();
	CHECK(dialog.size() == phoneSize);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: buttons wait for a complete phone number and code")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	QPushButton *send = FindButton(dialog, Text("Login.SendCode"));
	QPushButton *submit = FindButton(dialog, Text("Login.Submit"));
	REQUIRE(send);
	REQUIRE(submit);
	CHECK_FALSE(send->isEnabled());
	CHECK_FALSE(submit->isEnabled());

	FillPhoneLogin(dialog, QStringLiteral("09123"), QString());
	CHECK_FALSE(send->isEnabled());
	FillPhoneLogin(dialog, QStringLiteral("0912345678"), QStringLiteral("12345"));
	CHECK(send->isEnabled());
	CHECK_FALSE(submit->isEnabled());
	FillPhoneLogin(dialog, QStringLiteral("0912345678"), QStringLiteral("123456"));
	CHECK_FALSE(submit->isEnabled());                                               // no code sent yet
	FillPhoneLogin(dialog, QStringLiteral("0223456789"), QStringLiteral("123456")); // a landline
	CHECK_FALSE(send->isEnabled());
	CHECK_FALSE(submit->isEnabled());
}

TEST_CASE_FIXTURE(ControllerFixture, "login: Log in only after a code was sent to this number")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	QPushButton *submit = FindButton(dialog, Text("Login.Submit"));
	REQUIRE(submit);

	SendCodeAndFill(dialog, QStringLiteral("0912345678"), QStringLiteral("123456"));
	CHECK(submit->isEnabled());
	// The same number without its trunk 0 is the same number.
	FillPhoneLogin(dialog, QStringLiteral("912345678"), QStringLiteral("123456"));
	CHECK(submit->isEnabled());

	// Another number, or another country code: the code was not sent there.
	FillPhoneLogin(dialog, QStringLiteral("0912345679"), QStringLiteral("123456"));
	CHECK_FALSE(submit->isEnabled());
	FillPhoneLogin(dialog, QStringLiteral("0912345678"), QStringLiteral("123456"));
	CHECK(submit->isEnabled());
	auto *country = dialog.findChild<QComboBox *>();
	REQUIRE(country);
	REQUIRE(country->count() > 1);
	country->setCurrentIndex(country->currentIndex() == 0 ? 1 : 0);
	CHECK_FALSE(submit->isEnabled());
}

TEST_CASE_FIXTURE(ControllerFixture, "login: a code that could not be sent does not enable Log in")
{
	FakeLangLiveApi api;
	api.sendCodeError = {ApiError::Kind::Network, "", "timeout"};
	controller.SetApi(&api);
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	SendCodeAndFill(dialog, QStringLiteral("912345678"), QStringLiteral("123456"));
	CHECK_FALSE(FindButton(dialog, Text("Login.Submit"))->isEnabled());
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: requesting a code starts the resend countdown")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	dialog.findChildren<QLineEdit *>().first()->setText(QStringLiteral("912345678"));

	QPushButton *send = FindButton(dialog, Text("Login.SendCode"));
	REQUIRE(send);
	send->click();
	CHECK_FALSE(send->isEnabled());
	CHECK(send->text() == Text("Login.ResendIn").arg(90));
}

TEST_CASE_FIXTURE(ControllerFixture, "login: country codes show the region like the website, send the code only")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	QComboBox *country = dialog.findChildren<QComboBox *>().value(0);
	REQUIRE(country);
	CHECK(country->currentText() == QStringLiteral("TW+886"));
	CHECK(country->findText(QStringLiteral("MC+853")) >= 0);

	country->setCurrentIndex(country->findText(QStringLiteral("HK+852")));
	dialog.findChildren<QLineEdit *>().first()->setText(QStringLiteral("91234567"));
	FindButton(dialog, Text("Login.SendCode"))->click();
	CHECK(AllText(dialog).contains(Text("Login.SmsCodeSent").arg(QStringLiteral("+852 91234567"))));
}

TEST_CASE_FIXTURE(ControllerFixture, "login: Send code lines up with the code field, also under OBS's theme")
{
	// The rules of OBS's default theme (Yami, macOS) that moved the button:
	// every QPushButton gets a margin and a height cap below its size hint.
	const QString previous = qApp->styleSheet();
	qApp->setStyleSheet(QStringLiteral("QPushButton { height: 26.4px; max-height: 26.4px; margin-top: 1.6px; "
					   "margin-bottom: 1.6px; padding: 4px 16px; } "
					   "QLineEdit { padding: 4px 8px; height: 26.4px; }"));
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	ProcessEvents();
	QLineEdit *code = dialog.findChildren<QLineEdit *>().value(1);
	QPushButton *send = FindButton(dialog, Text("Login.SendCode"));
	REQUIRE(code);
	REQUIRE(send);
	CHECK(send->height() == code->height());
	CHECK(send->mapTo(&dialog, QPoint()).y() == code->mapTo(&dialog, QPoint()).y());
	qApp->setStyleSheet(previous);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: a phone number in the wrong format says so while typing")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	QLineEdit *phone = dialog.findChildren<QLineEdit *>().value(0);
	REQUIRE(phone);
	auto formatHintShown = [&]() {
		for (QLabel *label : dialog.findChildren<QLabel *>(QStringLiteral("errorLabel")))
			if (label->isVisible() && label->text() == Text("Login.InvalidPhone"))
				return true;
		return false;
	};
	CHECK_FALSE(formatHintShown());

	phone->setText(QStringLiteral("0912")); // too short for +886
	CHECK(formatHintShown());
	CHECK(phone->property("required").toBool()); // the red border
	// Under the number field, where its text starts too.
	for (QLabel *label : dialog.findChildren<QLabel *>(QStringLiteral("errorLabel")))
		if (label->isVisible() && label->text() == Text("Login.InvalidPhone"))
			CHECK(label->mapTo(&dialog, label->contentsRect().topLeft()).x() ==
			      phone->mapTo(&dialog, QPoint()).x());

	phone->setText(QStringLiteral("0912345678"));
	CHECK_FALSE(formatHintShown());
	CHECK_FALSE(phone->property("required").toBool());

	// The same number is wrong for another country code.
	QComboBox *country = dialog.findChildren<QComboBox *>().value(0);
	REQUIRE(country);
	country->setCurrentIndex(country->findText(QStringLiteral("HK+852")));
	CHECK(formatHintShown());

	// Emptied: back to "Required" once left.
	phone->clear();
	CHECK_FALSE(formatHintShown());
	QFocusEvent out(QEvent::FocusOut, Qt::MouseFocusReason);
	QCoreApplication::sendEvent(phone, &out);
	bool requiredShown = false;
	for (QLabel *label : dialog.findChildren<QLabel *>(QStringLiteral("errorLabel")))
		requiredShown = requiredShown || (label->isVisible() && label->text() == Text("Login.Required"));
	CHECK(requiredShown);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: a field left empty says it is required, like the website")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
	REQUIRE(edits.size() >= 2);
	QLineEdit *phone = edits[0];
	QLineEdit *code = edits[1];
	auto requiredShown = [&]() {
		int shown = 0;
		for (QLabel *label : dialog.findChildren<QLabel *>(QStringLiteral("errorLabel")))
			if (label->isVisible() && label->text() == Text("Login.Required"))
				++shown;
		return shown;
	};
	CHECK(requiredShown() == 0); // not before the streamer has been in the field
	QFocusEvent away(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
	QCoreApplication::sendEvent(edits[1], &away); // switching to another app
	CHECK(requiredShown() == 0);

	// Clicked into and out of again without typing: no editingFinished in
	// Qt 6, only the focus change.
	auto leave = [](QLineEdit *edit) {
		QFocusEvent out(QEvent::FocusOut, Qt::MouseFocusReason);
		QCoreApplication::sendEvent(edit, &out);
	};
	leave(code);
	CHECK(requiredShown() == 1);
	CHECK(code->property("required").toBool());

	leave(phone);
	CHECK(requiredShown() == 2);

	code->setText(QStringLiteral("1"));
	CHECK(requiredShown() == 1);
	CHECK_FALSE(code->property("required").toBool());
	phone->setText(QStringLiteral("9"));
	CHECK(requiredShown() == 0);

	phone->setText(QStringLiteral("912345678"));
	leave(phone); // filled in: nothing to say
	CHECK(requiredShown() == 0);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: a complete phone number marks the empty code as required")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
	REQUIRE(edits.size() >= 2);
	QLineEdit *code = edits[1];

	edits[0]->setText(QStringLiteral("91234")); // not complete yet
	CHECK_FALSE(code->property("required").toBool());
	edits[0]->setText(QStringLiteral("912345678"));
	CHECK(code->property("required").toBool());
	code->setText(QStringLiteral("12"));
	CHECK_FALSE(code->property("required").toBool());
}

TEST_CASE_FIXTURE(ControllerFixture, "login: number fields take digits without the input method")
{
	ui::LoginDialog dialog(controller, nullptr);
	const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
	REQUIRE(edits.size() >= 2);
	for (QLineEdit *edit : {edits[0], edits[1]}) {
		// Typing digits needs no switch away from a Chinese input method.
		CHECK_FALSE(edit->testAttribute(Qt::WA_InputMethodEnabled));
		REQUIRE(edit->validator());
	}

	int pos = 0;
	QString fullWidth = QStringLiteral("\uFF10\uFF19\uFF11\uFF16"); // ０９１６
	CHECK(edits[0]->validator()->validate(fullWidth, pos) == QValidator::Acceptable);
	CHECK(fullWidth == QStringLiteral("0916"));
	QString letters = QStringLiteral("09a");
	CHECK(edits[0]->validator()->validate(letters, pos) == QValidator::Invalid);
	QString tooLong = QStringLiteral("1234567");
	CHECK(edits[1]->validator()->validate(tooLong, pos) == QValidator::Invalid); // 6-digit code
}

TEST_CASE_FIXTURE(ControllerFixture, "login: today's SMS limit is shown and no countdown starts")
{
	for (int i = 0; i < 10; ++i)
		REQUIRE(RequestCode(controller, "+886", "912345678").result == SmsCodeRequest::Result::Sent);

	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	dialog.findChildren<QLineEdit *>().first()->setText(QStringLiteral("912345678"));

	QPushButton *send = FindButton(dialog, Text("Login.SendCode"));
	REQUIRE(send);
	send->click();
	CHECK(AllText(dialog).contains(Text("Login.SmsDailyLimit")));
	CHECK(send->isEnabled());
	CHECK(send->text() == Text("Login.SendCode"));
}

TEST_CASE_FIXTURE(ControllerFixture, "login: logging in shows a notice and closes the dialog")
{
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	SendCodeAndFill(dialog, QStringLiteral("912345678"), QStringLiteral("123456"));

	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("Login.Submit"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("Login.Success").arg(QStringLiteral("886912345678"))));
	CHECK(controller.IsLoggedIn());
	CHECK(dialog.result() == QDialog::Accepted);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: the backend's reason is shown, and buttons wait for its answer")
{
	FakeLangLiveApi api;
	controller.SetApi(&api);
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();
	SendCodeAndFill(dialog, QStringLiteral("912345678"), QStringLiteral("123456"));

	api.loginError = {ApiError::Kind::Other, "1", "Wrong code"};
	FindButton(dialog, Text("Login.Submit"))->click();
	CHECK(AllText(dialog).contains(QStringLiteral("Wrong code")));
	CHECK(FindButton(dialog, Text("Login.Submit"))->isEnabled());
	CHECK_FALSE(controller.IsLoggedIn());

	api.loginError = {ApiError::Kind::Network, "", "timeout"};
	FindButton(dialog, Text("Login.Submit"))->click();
	CHECK(AllText(dialog).contains(Text("Login.NetworkError")));

	api.loginError = {ApiError::Kind::NotEligible, "99302", ""};
	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("Login.Submit"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("Login.NotEligible")));
	CHECK(dialog.isVisible());
	controller.SetApi(nullptr);
}

namespace {

// Runs the event loop until `done` or `ms` milliseconds pass, for the QR
// code's 2-second checks. A notice that opens meanwhile is closed, its text
// added to `notice`.
bool RunUntil(const std::function<bool()> &done, int ms, QString *notice = nullptr)
{
	QEventLoop loop;
	QTimer poll;
	QElapsedTimer clock;
	clock.start();
	QObject::connect(&poll, &QTimer::timeout, [&]() {
		if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			if (notice)
				*notice += AllText(*modal);
			modal->done(QDialog::Accepted);
		}
		if (done() || clock.elapsed() > ms)
			loop.quit();
	});
	poll.start(20);
	loop.exec();
	return done();
}

} // namespace

TEST_CASE_FIXTURE(ControllerFixture, "login: the QR code logs in once the app confirms")
{
	FakeLangLiveApi api;
	// As the backend answers once the app scanned: 1801 once, then 1802
	// until the app confirms.
	api.qrStates = {{{}, {QrLoginState::Status::Scanned, {}, {}}},
			{{}, {QrLoginState::Status::Waiting, {}, {}}},
			{{}, {QrLoginState::Status::LoggedIn, "qr-login-token", "QR Streamer"}}};
	controller.SetApi(&api);
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();

	FindButton(dialog, Text("Login.TabQr"))->click();
	CHECK(api.calls == std::vector<std::string>{"qrcode/create"});
	CHECK(AllText(dialog).contains(Text("Login.QrExpiresIn").arg(QStringLiteral("5:00"))));
	CHECK_FALSE(FindButton(dialog, Text("Login.QrRefresh")));

	REQUIRE(RunUntil([&]() { return api.qrChecks >= 1; }, 3000));
	CHECK(AllText(dialog).contains(Text("Login.QrScanned")));
	REQUIRE(RunUntil([&]() { return api.qrChecks >= 2; }, 3000));
	CHECK(AllText(dialog).contains(Text("Login.QrScanned"))); // not back to the countdown

	QString notice;
	REQUIRE(RunUntil([&]() { return dialog.result() == QDialog::Accepted; }, 3000, &notice));
	CHECK(notice.contains(Text("Login.Success").arg(QStringLiteral("QR Streamer"))));
	CHECK(controller.IsLoggedIn());
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: an expired QR code, or one that could not be made, offers a refresh")
{
	FakeLangLiveApi api;
	api.qrCreateError = {ApiError::Kind::Network, "", "timeout"};
	api.qrStates = {{{}, {QrLoginState::Status::Expired, {}, {}}}};
	controller.SetApi(&api);
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();

	FindButton(dialog, Text("Login.TabQr"))->click();
	CHECK(AllText(dialog).contains(Text("Login.NetworkError")));
	QPushButton *refresh = FindButton(dialog, Text("Login.QrRefresh"));
	REQUIRE(refresh);

	api.qrCreateError = {};
	refresh->click();
	CHECK_FALSE(FindButton(dialog, Text("Login.QrRefresh")));
	REQUIRE(RunUntil([&]() { return api.qrChecks >= 1; }, 3000));
	CHECK(AllText(dialog).contains(Text("Login.QrExpired")));
	CHECK(FindButton(dialog, Text("Login.QrRefresh")));
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(ControllerFixture, "login: the QR code is checked only while its tab is shown")
{
	FakeLangLiveApi api;
	controller.SetApi(&api);
	ui::LoginDialog dialog(controller, nullptr);
	dialog.show();

	FindButton(dialog, Text("Login.TabQr"))->click();
	FindButton(dialog, Text("Login.TabPhone"))->click();
	RunUntil([]() { return false; }, 2500);
	CHECK(api.qrChecks == 0);
	controller.SetApi(nullptr);
}

// --- Pre-stream check ------------------------------------------------------------

TEST_CASE_FIXTURE(ControllerFixture, "preflight: lists every problem; scenario choice only for a wrong resolution")
{
	SUBCASE("resolution is right")
	{
		ui::PreflightDialog dialog(
			controller, {Violation::UnsupportedVideoCodec, Violation::KeyframeIntervalZero}, nullptr);
		CHECK(dialog.findChildren<QLabel *>(QStringLiteral("violation")).size() == 2);
		CHECK(dialog.findChildren<QRadioButton *>().isEmpty());
	}
	SUBCASE("resolution is wrong")
	{
		ui::PreflightDialog dialog(controller, {Violation::LandscapeResolutionMismatch}, nullptr);
		CHECK(dialog.findChildren<QLabel *>(QStringLiteral("violation")).size() == 1);
		CHECK(dialog.findChildren<QRadioButton *>(QStringLiteral("scenarioCard")).size() == 2);
	}
}

TEST_CASE_FIXTURE(ControllerFixture, "preflight: Apply and go live applies the chosen scenario and starts")
{
	// A live from the go-live dialog, stopped by the check when it started.
	LogIn();
	GoLiveRequest request;
	request.settings.title = "Tonight";
	request.channelId = "mock-channel-1";
	REQUIRE(testing::StartLive(controller, request).started());
	host.state.outputWidth = 1000;
	REQUIRE_FALSE(controller.OnStreamingStarted().empty());
	controller.OnStreamingStopped();

	ui::PreflightDialog dialog(controller, {Violation::LandscapeResolutionMismatch}, nullptr);
	dialog.show();
	for (QRadioButton *card : dialog.findChildren<QRadioButton *>(QStringLiteral("scenarioCard")))
		if (card->text() == Text("Scenario.HighQuality"))
			card->setChecked(true);

	FindButton(dialog, Text("Preflight.ApplyAndStart"))->click();
	REQUIRE(host.appliedSpec.has_value());
	CHECK(host.appliedSpec->width == 1920);
	CHECK(host.startCount == 2); // the same live again
	CHECK(dialog.result() == QDialog::Accepted);
}

TEST_CASE_FIXTURE(ControllerFixture, "preflight: nothing starts when the settings cannot be applied")
{
	host.applySucceeds = false;
	ui::PreflightDialog dialog(controller, {Violation::UnsupportedVideoCodec}, nullptr);
	dialog.show();

	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("Preflight.ApplyAndStart"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("Preflight.ApplyFailed")));
	CHECK(host.startCount == 0);
	CHECK(dialog.isVisible());
	CHECK(FindButton(dialog, Text("Preflight.ApplyAndStart"))->isEnabled());
}

// --- Updates ---------------------------------------------------------------------

TEST_CASE_FIXTURE(ControllerFixture, "update: an optional update can be postponed")
{
	UpdateCheck check;
	check.result = UpdateCheck::Result::Available;
	check.latestVersion = "0.2.0";
	ui::UpdateDialog dialog(controller, check, nullptr);
	dialog.show();

	REQUIRE(FindButton(dialog, Text("Update.Later")));
	FindButton(dialog, Text("Update.Later"))->click();
	CHECK_FALSE(dialog.isVisible());
}

TEST_CASE_FIXTURE(ControllerFixture, "update: a required update has no Later, but can be closed")
{
	UpdateCheck check;
	check.result = UpdateCheck::Result::Required;
	check.latestVersion = "2.0.0";
	ui::UpdateDialog dialog(controller, check, nullptr);
	dialog.show();

	CHECK_FALSE(FindButton(dialog, Text("Update.Later")));
	CHECK(FindButton(dialog, Text("Update.Now")));
	const QList<QPushButton *> close = dialog.findChildren<QPushButton *>(QStringLiteral("closeButton"));
	REQUIRE(close.size() == 1);
	close.front()->click();
	CHECK_FALSE(dialog.isVisible());
}

namespace {

UpdateManifest TestManifest(const char *version, const char *minSupported)
{
	UpdateManifest manifest;
	manifest.version = version;
	manifest.minSupportedVersion = minSupported;
	manifest.installerUrl = "https://example.com/installer.pkg";
	manifest.installerSha256 = std::string(64, 'a');
	return manifest;
}

// The LangLive menu's item with the text `key`.
QAction *MenuAction(QMainWindow &window, const char *key)
{
	QMenu *menu = window.menuBar()->findChildren<QMenu *>().value(0);
	if (menu)
		for (QAction *action : menu->actions())
			if (action->text() == Text(key))
				return action;
	FAIL("no menu item " << key);
	return nullptr;
}

// The update dialog the menu opened (window-modal with open(), not exec()).
ui::UpdateDialog *OpenUpdateDialog(QMainWindow &window)
{
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	for (ui::UpdateDialog *dialog : window.findChildren<ui::UpdateDialog *>())
		if (dialog->isVisible())
			return dialog;
	return nullptr;
}

} // namespace

TEST_CASE_FIXTURE(ControllerFixture, "menu: an optional update is shown only by Check for Updates")
{
	FakeUpdateFeed feed;
	feed.manifest = TestManifest("2.0.0", "1.0.0");
	controller.SetUpdateFeed(&feed, "1.0.0");
	QMainWindow window;
	ui::LangLiveMenu menu(controller, &window);
	CHECK(feed.fetchCount == 0); // nothing when OBS starts

	// The menu items check, and go on without showing it.
	CHECK(WithModal([&]() { MenuAction(window, "Menu.Login")->trigger(); },
			[&](QDialog &modal) { CHECK(qobject_cast<ui::LoginDialog *>(&modal)); }));
	CHECK(feed.fetchCount == 1);
	CHECK_FALSE(OpenUpdateDialog(window));

	MenuAction(window, "Menu.CheckForUpdates")->trigger();
	ui::UpdateDialog *dialog = OpenUpdateDialog(window);
	REQUIRE(dialog);
	CHECK(AllText(*dialog).contains(Text("Update.AvailableTitle")));
	dialog->reject();
	controller.SetUpdateFeed(nullptr, "");
}

TEST_CASE_FIXTURE(ControllerFixture, "menu: a required update is checked from every item")
{
	FakeUpdateFeed feed;
	feed.manifest = TestManifest("2.0.0", "2.0.0");
	controller.SetUpdateFeed(&feed, "1.0.0");
	QMainWindow window;
	ui::LangLiveMenu menu(controller, &window);
	const auto noModal = [](QDialog &) {
	};

	SUBCASE("log in: the update instead, also after it is closed")
	{
		MenuAction(window, "Menu.Login")->trigger();
		CHECK(feed.fetchCount == 1);
		ui::UpdateDialog *dialog = OpenUpdateDialog(window);
		REQUIRE(dialog);
		CHECK(AllText(*dialog).contains(Text("Update.RequiredTitle")));
		CHECK_FALSE(WithModal([&]() { dialog->reject(); }, noModal)); // the X: no login dialog

		MenuAction(window, "Menu.Login")->trigger();
		CHECK(feed.fetchCount == 2);
		CHECK(OpenUpdateDialog(window));
	}
	SUBCASE("the check fails: still blocked")
	{
		MenuAction(window, "Menu.Login")->trigger();
		REQUIRE(OpenUpdateDialog(window));
		OpenUpdateDialog(window)->reject();

		feed.manifest.reset();
		MenuAction(window, "Menu.Login")->trigger();
		ui::UpdateDialog *dialog = OpenUpdateDialog(window);
		REQUIRE(dialog);
		CHECK(AllText(*dialog).contains(Text("Update.RequiredTitle")));
	}
	SUBCASE("no longer required: login opens")
	{
		MenuAction(window, "Menu.Login")->trigger();
		REQUIRE(OpenUpdateDialog(window));
		OpenUpdateDialog(window)->reject();

		feed.manifest = TestManifest("2.0.0", "1.0.0");
		CHECK(WithModal([&]() { MenuAction(window, "Menu.Login")->trigger(); },
				[&](QDialog &modal) { CHECK(qobject_cast<ui::LoginDialog *>(&modal)); }));
		CHECK_FALSE(controller.UpdateRequired());
	}
	SUBCASE("diagnostics: the update, then diagnostics once it is closed")
	{
		MenuAction(window, "Menu.Diagnostics")->trigger();
		ui::UpdateDialog *dialog = OpenUpdateDialog(window);
		REQUIRE(dialog);
		CHECK(WithModal([&]() { dialog->reject(); },
				[&](QDialog &modal) { CHECK(qobject_cast<ui::DiagnosticsDialog *>(&modal)); }));
	}
	SUBCASE("log out: the update, then the logout question once it is closed")
	{
		LogIn();
		menu.Refresh();
		MenuAction(window, "Menu.Logout")->trigger();
		ui::UpdateDialog *dialog = OpenUpdateDialog(window);
		REQUIRE(dialog);
		CHECK(WithModal([&]() { dialog->reject(); },
				[&](QDialog &modal) { CHECK(AllText(modal).contains(Text("Logout.Question"))); }));
		CHECK(controller.IsLoggedIn()); // the question was canceled
	}
	controller.SetUpdateFeed(nullptr, "");
}

TEST_CASE_FIXTURE(ControllerFixture, "menu: a required update blocks go-live, but never during a plugin live")
{
	LogIn();
	FakeUpdateFeed feed;
	controller.SetUpdateFeed(&feed, "1.0.0");
	QMainWindow window;
	ui::LangLiveMenu menu(controller, &window);

	SUBCASE("a login saved before the update became required cannot go live")
	{
		feed.manifest = TestManifest("2.0.0", "2.0.0");
		MenuAction(window, "Menu.GoLive")->trigger();
		ui::UpdateDialog *dialog = OpenUpdateDialog(window);
		REQUIRE(dialog);
		dialog->reject();
		CHECK(window.findChildren<ui::GoLiveDialog *>().isEmpty());
	}
	SUBCASE("during a plugin live it opens at once, so the live can be ended")
	{
		GoLiveRequest request;
		request.settings.title = "Tonight";
		request.channelId = "mock-channel-1";
		REQUIRE(testing::StartLive(controller, request).started());
		REQUIRE(controller.OnStreamingStarted().empty());
		feed.manifest = TestManifest("2.0.0", "2.0.0"); // published during the live

		MenuAction(window, "Menu.GoLive")->trigger();
		CHECK(feed.fetchCount == 0);
		CHECK_FALSE(OpenUpdateDialog(window));
		CHECK(window.findChildren<ui::GoLiveDialog *>().size() == 1);
		controller.OnStreamingStopped();
	}
	controller.SetUpdateFeed(nullptr, "");
}

TEST_CASE_FIXTURE(ControllerFixture, "update: closing with X during the download cancels it")
{
	FakeUpdateFeed feed;
	feed.manifest = TestManifest("2.0.0", "1.0.0");
	feed.finishDownloads = false;
	controller.SetUpdateFeed(&feed, "1.0.0");
	std::optional<UpdateCheck> check;
	controller.CheckForUpdates([&](UpdateCheck c) { check = c; });
	REQUIRE(check.has_value());

	ui::UpdateDialog dialog(controller, *check, nullptr);
	dialog.show();
	FindButton(dialog, Text("Update.Now"))->click();
	REQUIRE(feed.downloadCount == 1);
	CHECK_FALSE(FindButton(dialog, Text("Update.Now"))->isEnabled()); // downloading

	const QList<QPushButton *> close = dialog.findChildren<QPushButton *>(QStringLiteral("closeButton"));
	REQUIRE(close.size() == 1);
	close.front()->click();
	CHECK_FALSE(dialog.isVisible());
	CHECK(feed.cancelCount == 1);
	feed.FinishDownload(); // nothing is shown for the aborted download
	controller.SetUpdateFeed(nullptr, "");
}

TEST_CASE_FIXTURE(ControllerFixture, "update: a checksum mismatch says the installer failed verification")
{
	FakeUpdateFeed feed;
	UpdateManifest manifest;
	manifest.version = "0.2.0";
	manifest.minSupportedVersion = "0.1.0";
	manifest.installerUrl = "https://example.com/0.2.0/installer.pkg";
	manifest.installerSha256 = std::string(64, '0'); // well-formed, but not the download's
	feed.manifest = manifest;
	feed.downloadResult = InstallerDownload::ChecksumMismatch;
	controller.SetUpdateFeed(&feed, "0.1.4");
	UpdateCheck check;
	controller.CheckForUpdates([&](UpdateCheck c) { check = c; });
	REQUIRE(check.result == UpdateCheck::Result::Available);

	ui::UpdateDialog dialog(controller, check, nullptr);
	dialog.show();
	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("Update.Now"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("Update.ChecksumMismatch")));
	CHECK(feed.launchedPath.empty());
	CHECK(dialog.isVisible());
	controller.SetUpdateFeed(nullptr, "");
}

// --- Notices and menu ------------------------------------------------------------

TEST_CASE("notice: shows the message and closes with OK")
{
	QString text;
	bool ok = false;
	CHECK(WithModal([]() { ui::ShowMessage(nullptr, QStringLiteral("Hello")); },
			[&](QDialog &modal) {
				text = AllText(modal);
				ok = FindButton(modal, Text("Message.OK")) != nullptr;
			}));
	CHECK(text.contains(QStringLiteral("Hello")));
	CHECK(ok);
}

TEST_CASE("confirmation: confirm returns true; cancel, close and Escape return false")
{
	const QString title = QStringLiteral("Title");
	const QString confirmText = QStringLiteral("Do it");
	auto ask = [&](const std::function<void(QDialog &)> &act) {
		bool result = true;
		REQUIRE(WithModal(
			[&]() { result = ui::AskConfirmation(nullptr, title, QStringLiteral("Sure?"), confirmText); },
			act));
		return result;
	};

	CHECK(ask([&](QDialog &modal) {
		CHECK(AllText(modal).contains(QStringLiteral("Sure?")));
		FindButton(modal, confirmText)->click();
	}));
	CHECK_FALSE(ask([&](QDialog &modal) { FindButton(modal, Text("Message.Cancel"))->click(); }));
	CHECK_FALSE(ask([&](QDialog &modal) { FindButton(modal, QStringLiteral("✕"))->click(); }));
	CHECK_FALSE(ask([](QDialog &) {})); // closed without a choice, like Escape
}

TEST_CASE_FIXTURE(ControllerFixture, "menu: logging out with a dialog open over go-live waits to delete it")
{
	QMainWindow window;
	ui::LangLiveMenu menu(controller, &window);
	QMenu *langlive = window.menuBar()->findChildren<QMenu *>().value(0);
	REQUIRE(langlive);
	QAction *goLive = nullptr;
	for (QAction *a : langlive->actions())
		if (a->text() == Text("Menu.GoLive"))
			goLive = a;
	REQUIRE(goLive);

	LogIn();
	menu.Refresh();
	goLive->trigger();
	QPointer<ui::GoLiveDialog> dialog = window.findChildren<ui::GoLiveDialog *>().value(0);
	REQUIRE(dialog);

	// A notice over the go-live dialog when the login expires (any of its
	// modal children: the pre-stream check, the cover crop, ...).
	CHECK(WithModal([&]() { ui::ShowMessage(dialog, QStringLiteral("child")); },
			[&](QDialog &) {
				controller.Logout();
				menu.Refresh();
				for (int i = 0; i < 3; ++i) {
					QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
					QCoreApplication::processEvents();
				}
				CHECK(dialog); // still alive under its child
				CHECK_FALSE(dialog->isVisible());
			}));

	// Deleted once no dialog is open.
	QElapsedTimer waited;
	waited.start();
	while (dialog && waited.elapsed() < 3000) {
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QThread::msleep(10);
	}
	CHECK_FALSE(dialog);

	// The next login gets a new form.
	LogIn();
	menu.Refresh();
	goLive->trigger();
	CHECK(window.findChildren<ui::GoLiveDialog *>().size() == 1);
}

TEST_CASE_FIXTURE(ControllerFixture, "menu: a lost login says why")
{
	QMainWindow window;
	ui::LangLiveMenu menu(controller, &window);
	QString notice;
	CHECK(WithModal([&]() { menu.ShowLoginLost(ApiError::Kind::SessionExpired); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("Login.SessionExpired")));
	CHECK(WithModal([&]() { menu.ShowLoginLost(ApiError::Kind::NotEligible); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("Login.NotEligible")));
}

TEST_CASE_FIXTURE(ControllerFixture, "menu: a QA build is named (Test)")
{
	QMainWindow production;
	ui::LangLiveMenu productionMenu(controller, &production);
	CHECK(production.menuBar()->findChildren<QMenu *>().value(0)->title() == Text("Brand"));

	QMainWindow qa;
	ui::LangLiveMenu qaMenu(controller, &qa, true);
	CHECK(qa.menuBar()->findChildren<QMenu *>().value(0)->title() == Text("Brand") + QStringLiteral("(Test)"));
}

TEST_CASE_FIXTURE(ControllerFixture, "menu: follows the login state")
{
	QMainWindow window;
	ui::LangLiveMenu menu(controller, &window);
	QMenu *langlive = window.menuBar()->findChildren<QMenu *>().value(0);
	REQUIRE(langlive);

	auto action = [&](const char *key) -> QAction * {
		for (QAction *a : langlive->actions())
			if (a->text() == Text(key))
				return a;
		return nullptr;
	};

	REQUIRE(action("Menu.Login"));
	CHECK_FALSE(action("Menu.GoLive")->isEnabled());

	LogIn();
	menu.Refresh();
	CHECK(action("Menu.Logout"));
	CHECK(action("Menu.GoLive")->isEnabled());

	SUBCASE("logging out asks first")
	{
		CHECK(WithModal([&]() { action("Menu.Logout")->trigger(); },
				[&](QDialog &modal) {
					CHECK(AllText(modal).contains(Text("Logout.Question")));
					FindButton(modal, Text("Message.Cancel"))->click();
				}));
		CHECK(controller.IsLoggedIn());

		CHECK(WithModal([&]() { action("Menu.Logout")->trigger(); },
				[&](QDialog &modal) { FindButton(modal, Text("Logout.Confirm"))->click(); }));
		CHECK_FALSE(controller.IsLoggedIn());
	}

	SUBCASE("logout is disabled during a plugin live, and enabled again when it ends")
	{
		GoLiveRequest request;
		request.settings.title = "Tonight";
		request.channelId = "mock-channel-1";
		REQUIRE(testing::StartLive(controller, request).started());
		REQUIRE(controller.OnStreamingStarted().empty());
		menu.Refresh();
		CHECK_FALSE(action("Menu.Logout")->isEnabled());

		action("Menu.Logout")->trigger(); // disabled: nothing happens
		CHECK(controller.IsLoggedIn());
		CHECK(host.stopCount == 0);

		controller.OnStreamingStopped();
		menu.Refresh();
		CHECK(action("Menu.Logout")->isEnabled());
	}

	SUBCASE("a plugin live stopped in OBS shows a notice with OK only")
	{
		GoLiveRequest request;
		request.settings.title = "Tonight";
		request.channelId = "mock-channel-1";
		REQUIRE(testing::StartLive(controller, request).started());
		REQUIRE(controller.OnStreamingStarted().empty());
		controller.OnStreamingStopped(); // OBS's Stop Streaming

		CHECK(WithModal([&]() { menu.ShowLiveEndedOutside(); },
				[&](QDialog &modal) {
					CHECK(AllText(modal).contains(Text("LiveEnded.Title")));
					CHECK(AllText(modal).contains(Text("LiveEnded.Outside")));
					CHECK(FindButton(modal, Text("LiveEnded.OK")));
					CHECK_FALSE(FindButton(modal, Text("Message.Cancel")));
				}));
	}

	SUBCASE("streaming with a website stream key disables go-live, not logout")
	{
		REQUIRE(controller.OnStreamingStarted().empty()); // started from OBS
		menu.Refresh();
		CHECK_FALSE(action("Menu.GoLive")->isEnabled());
		CHECK(action("Menu.Logout")->isEnabled());
	}
}

TEST_CASE("diagnostics: the export result's message fits on one line, up to the widest dialog")
{
	ui::DiagnosticsExportedDialog dialog(QStringLiteral("/tmp/langlive_diagnostics_20261118.zip"), nullptr);
	dialog.show();
	QLabel *message = nullptr;
	for (QLabel *label : dialog.findChildren<QLabel *>())
		if (label->text() == Text("Diag.Exported"))
			message = label;
	REQUIRE(message);
	// One line when the dialog can be that wide (the Chinese message); a
	// longer one (the English) wraps in the widest dialog instead.
	const bool oneLine = message->height() < 2 * message->fontMetrics().lineSpacing();
	CHECK((oneLine || dialog.width() == 640));
	CHECK(dialog.width() <= 640);
	CHECK(dialog.width() >= 480);
}
