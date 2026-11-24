#include "ui/langlive-menu.hpp"

#include "app/plugin-controller.hpp"
#include "ui/diagnostics-dialog.hpp"
#include "ui/go-live-dialog.hpp"
#include "ui/login-dialog.hpp"
#include "ui/preflight-dialog.hpp"
#include "ui/message-dialog.hpp"
#include "ui/text.hpp"
#include "ui/update-dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QMainWindow>
#include <QTimer>
#include <QMenu>
#include <QMenuBar>
#include <QUrl>

#include <utility>

namespace langlive::obsplugin::ui {

namespace {

// Deletes `widget` once no modal dialog is open. The go-live dialog can lose
// its account while one of its own modal children runs (e.g. a reply saying
// the login expired arrives while the pre-stream check or a notice is
// open); deleting it then would delete that running child under its event
// loop.
void DeleteWhenNoModal(QPointer<QWidget> widget)
{
	if (!widget)
		return;
	if (!QApplication::activeModalWidget()) {
		widget->deleteLater();
		return;
	}
	QTimer::singleShot(200, widget, [widget]() { DeleteWhenNoModal(widget); });
}

} // namespace

LangLiveMenu::LangLiveMenu(PluginController &controller, QMainWindow *mainWindow, bool qaBuild)
	: QObject(mainWindow),
	  controller_(controller),
	  mainWindow_(mainWindow)
{
	QMenu *menu = mainWindow_->menuBar()->addMenu(Text("Brand") + (qaBuild ? QStringLiteral("(Test)") : QString()));

	loginAction_ = menu->addAction(QString());
	goLiveAction_ = menu->addAction(Text("Menu.GoLive"));
	menu->addSeparator();
	QAction *diagnostics = menu->addAction(Text("Menu.Diagnostics"));
	QAction *help = menu->addAction(Text("Menu.HelpCenter"));
	QAction *updates = menu->addAction(Text("Menu.CheckForUpdates"));

	connect(loginAction_, &QAction::triggered, this, &LangLiveMenu::OnLoginTriggered);
	connect(goLiveAction_, &QAction::triggered, this, &LangLiveMenu::OnGoLiveTriggered);
	connect(diagnostics, &QAction::triggered, this, &LangLiveMenu::OnDiagnosticsTriggered);
	connect(help, &QAction::triggered, this, &LangLiveMenu::OnHelpTriggered);
	connect(updates, &QAction::triggered, this, &LangLiveMenu::OnCheckForUpdatesTriggered);

	Refresh();
}

LangLiveMenu::~LangLiveMenu()
{
	// A check still running when OBS exits never answers.
	if (checkingForUpdates_)
		QApplication::restoreOverrideCursor();
}

void LangLiveMenu::Refresh()
{
	loginAction_->setText(Text(controller_.IsLoggedIn() ? "Menu.Logout" : "Menu.Login"));
	// No logout during a plugin live: the streamer ends the live first.
	loginAction_->setEnabled(!controller_.IsLoggedIn() || controller_.CanLogout());
	goLiveAction_->setEnabled(controller_.CanOpenGoLive());

	if (goLiveDialog_ && !controller_.IsLoggedIn()) {
		// The form belongs to the account; the next login starts fresh.
		goLiveDialog_->hide();
		DeleteWhenNoModal(goLiveDialog_.data());
		goLiveDialog_ = nullptr;
	}

	if (goLiveDialog_) {
		// Streaming with a stream key from the website: the plugin must
		// not offer a second way to go live.
		if (!controller_.CanOpenGoLive())
			goLiveDialog_->close();
		else
			goLiveDialog_->Refresh();
	}
}

void LangLiveMenu::ShowPreflightFailure(std::vector<core::Violation> violations)
{
	if (goLiveDialog_)
		goLiveDialog_->CancelPendingClose();

	// Over the go-live dialog when the live was started from it.
	QWidget *parent = goLiveDialog_ && goLiveDialog_->isVisible() ? static_cast<QWidget *>(goLiveDialog_)
								      : mainWindow_;
	PreflightDialog dialog(controller_, std::move(violations), parent);
	if (dialog.exec() == QDialog::Accepted && goLiveDialog_ && goLiveDialog_->isVisible())
		goLiveDialog_->CloseWhenLive();

	// Back to the go-live dialog so the streamer can start again. macOS
	// otherwise brings OBS's main window to the front over it.
	if (goLiveDialog_ && goLiveDialog_->isVisible()) {
		QPointer<GoLiveDialog> goLive = goLiveDialog_;
		QTimer::singleShot(0, goLive, [goLive]() {
			goLive->raise();
			goLive->activateWindow();
		});
	}
}

void LangLiveMenu::ShowLoginLost(ApiError::Kind reason)
{
	QWidget *parent = goLiveDialog_ && goLiveDialog_->isVisible() ? static_cast<QWidget *>(goLiveDialog_)
								      : mainWindow_;
	ShowMessage(parent, Text(reason == ApiError::Kind::NotEligible ? "Login.NotEligible" : "Login.SessionExpired"));
}

void LangLiveMenu::ShowLiveEndedOutside()
{
	QWidget *parent = goLiveDialog_ && goLiveDialog_->isVisible() ? static_cast<QWidget *>(goLiveDialog_)
								      : mainWindow_;
	ShowMessage(parent, Text("LiveEnded.Outside"), Text("LiveEnded.Title"), Text("LiveEnded.OK"));
}

void LangLiveMenu::OnLoginTriggered()
{
	if (controller_.IsLoggedIn()) {
		CheckThen(WhenRequired::RunAfterDialog, [this]() {
			if (controller_.CanLogout() &&
			    AskConfirmation(mainWindow_, Text("Logout.Title"), Text("Logout.Question"),
					    Text("Logout.Confirm")) &&
			    controller_.CanLogout()) // a live may have started while asking
				controller_.Logout();
		});
		return;
	}

	// An unsupported version cannot log in; it gets the update instead.
	CheckThen(WhenRequired::Block, [this]() {
		LoginDialog dialog(controller_, mainWindow_);
		dialog.exec();
	});
}

void LangLiveMenu::OnGoLiveTriggered()
{
	// During a plugin live the go-live dialog is where it ends: never in
	// the way of that.
	if (controller_.CurrentLiveSource() == LiveSource::Plugin || controller_.IsStartingLive()) {
		OpenGoLive();
		return;
	}
	// Also for a login saved before the update became required.
	CheckThen(WhenRequired::Block, [this]() { OpenGoLive(); });
}

void LangLiveMenu::OpenGoLive()
{
	if (!controller_.CanOpenGoLive())
		return;
	// Closing only hides the dialog, so the form (including the events and
	// channel, which are not saved to disk) is kept until logout or OBS
	// exits.
	if (!goLiveDialog_)
		goLiveDialog_ = new GoLiveDialog(controller_, mainWindow_);
	else {
		goLiveDialog_->Refresh();
		goLiveDialog_->ReloadInfo();
	}
	goLiveDialog_->show();
	goLiveDialog_->raise();
	goLiveDialog_->activateWindow();
}

void LangLiveMenu::OnDiagnosticsTriggered()
{
	// Still there during a required update, e.g. to send the logs when
	// updating fails.
	CheckThen(WhenRequired::RunAfterDialog, [this]() {
		DiagnosticsDialog dialog(mainWindow_);
		dialog.exec();
	});
}

void LangLiveMenu::OnHelpTriggered()
{
	CheckThen(WhenRequired::RunAfterDialog, [this]() {
		QDesktopServices::openUrl(
			QUrl(QString::fromStdString(controller_.WebsitePageUrl(core::WebsitePage::HelpCenter))));
	});
}

// The only place an optional update is shown.
void LangLiveMenu::OnCheckForUpdatesTriggered()
{
	if (checkingForUpdates_)
		return;
	checkingForUpdates_ = true;
	QApplication::setOverrideCursor(Qt::WaitCursor);

	QPointer<LangLiveMenu> self = this;
	controller_.CheckForUpdates([self](UpdateCheck check) {
		if (!self)
			return;
		self->checkingForUpdates_ = false;
		QApplication::restoreOverrideCursor();
		// A failed check keeps the last result: a required update stays
		// required.
		if (check.result != UpdateCheck::Result::Failed)
			self->lastUpdateCheck_ = check;
		switch (check.result) {
		case UpdateCheck::Result::Available:
		case UpdateCheck::Result::Required:
			self->ShowUpdateDialog(check);
			break;
		case UpdateCheck::Result::UpToDate:
			ShowMessage(
				self->mainWindow_,
				Text("Update.UpToDate").arg(QString::fromStdString(self->controller_.CurrentVersion())));
			break;
		case UpdateCheck::Result::Failed:
			ShowMessage(self->mainWindow_, Text("Update.Failed"));
			break;
		}
	});
}

void LangLiveMenu::CheckThen(WhenRequired whenRequired, std::function<void()> action)
{
	if (checkingForUpdates_)
		return;
	checkingForUpdates_ = true;
	// The check usually takes a moment; on a bad connection it can take
	// longer, and the item seems to do nothing meanwhile.
	QApplication::setOverrideCursor(Qt::WaitCursor);

	QPointer<LangLiveMenu> self = this;
	controller_.CheckForUpdates([self, whenRequired, action = std::move(action)](UpdateCheck check) {
		if (!self)
			return;
		self->checkingForUpdates_ = false;
		QApplication::restoreOverrideCursor();
		if (check.result != UpdateCheck::Result::Failed)
			self->lastUpdateCheck_ = check;
		if (!self->controller_.UpdateRequired()) {
			action();
			return;
		}
		UpdateDialog *dialog = self->ShowUpdateDialog(self->lastUpdateCheck_);
		// Closed (X), not updating: updating quits OBS.
		if (whenRequired == WhenRequired::RunAfterDialog)
			connect(dialog, &QDialog::rejected, self.data(), action, Qt::SingleShotConnection);
	});
}

UpdateDialog *LangLiveMenu::ShowUpdateDialog(const UpdateCheck &check)
{
	// A closed dialog is deleted later; it may still be here, hidden.
	if (updateDialog_ && updateDialog_->isVisible()) {
		updateDialog_->raise();
		updateDialog_->activateWindow();
		return updateDialog_;
	}

	updateDialog_ = new UpdateDialog(controller_, check, mainWindow_);
	updateDialog_->setAttribute(Qt::WA_DeleteOnClose);
	connect(updateDialog_, &UpdateDialog::QuitRequested, this, [this]() {
		// The installer waits for OBS to exit before it starts.
		QTimer::singleShot(0, mainWindow_, &QMainWindow::close);
	});
	updateDialog_->open();
	return updateDialog_;
}

} // namespace langlive::obsplugin::ui
