#include "ui/update-dialog.hpp"

#include "ui/message-dialog.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace langlive::obsplugin::ui {

namespace {

constexpr int kWidth = 480;

constexpr const char *kStyleSheet = R"(
	QLabel#updateMessage { font-size: 15px; }
	QProgressBar {
		background-color: {header};
		border: none;
		border-radius: 3px;
		max-height: 6px;
		min-height: 6px;
	}
	QProgressBar::chunk { background-color: {brand}; border-radius: 3px; }
)";

QPushButton *MakeButton(const QString &text, const char *objectName, QWidget *parent)
{
	auto *button = new QPushButton(text, parent);
	button->setObjectName(QString::fromUtf8(objectName));
	button->setCursor(Qt::PointingHandCursor);
	button->setAutoDefault(false);
	return button;
}

} // namespace

UpdateDialog::UpdateDialog(PluginController &controller, const UpdateCheck &check, QWidget *parent)
	: QDialog(parent),
	  controller_(controller),
	  required_(check.result == UpdateCheck::Result::Required)
{
	ApplyDialogStyle(this, kStyleSheet);
	const Card card = MakeCard(this, Text(required_ ? "Update.RequiredTitle" : "Update.AvailableTitle"), true);

	auto *body = new QVBoxLayout();
	body->setContentsMargins(28, 22, 28, 22);
	body->setSpacing(14);

	const QString latest = QString::fromStdString(check.latestVersion);
	const QString current = QString::fromStdString(controller_.CurrentVersion());
	auto *message = new QLabel(required_ ? Text("Update.RequiredMessage").arg(latest, current)
					     : Text("Update.AvailableMessage").arg(latest, current),
				   card.frame);
	message->setObjectName(QStringLiteral("updateMessage"));
	message->setWordWrap(true);
	body->addWidget(message);

	progress_ = new QProgressBar(card.frame);
	progress_->setRange(0, 100);
	progress_->setTextVisible(false);
	progress_->hide();
	body->addWidget(progress_);
	status_ = MakeHintLabel(QString(), card.frame);
	status_->hide();
	body->addWidget(status_);
	card.content->addLayout(body);

	card.content->addWidget(MakeDivider(card.frame));
	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(28, 14, 28, 16);
	footer->setSpacing(10);
	footer->addStretch();
	if (!required_) {
		laterButton_ = MakeButton(Text("Update.Later"), "secondaryButton", card.frame);
		footer->addWidget(laterButton_);
		connect(laterButton_, &QPushButton::clicked, this, &QDialog::reject);
	}
	updateButton_ = MakeButton(Text("Update.Now"), "primaryButton", card.frame);
	footer->addWidget(updateButton_);
	card.content->addLayout(footer);
	connect(updateButton_, &QPushButton::clicked, this, &UpdateDialog::OnUpdateClicked);

	FitCardHeight(this, kWidth);
}

void UpdateDialog::reject()
{
	if (downloading_) {
		downloading_ = false;
		controller_.CancelUpdateDownload();
	}
	QDialog::reject();
}

void UpdateDialog::SetBusy(bool busy)
{
	downloading_ = busy;
	updateButton_->setEnabled(!busy);
	if (laterButton_)
		laterButton_->setEnabled(!busy);
	progress_->setVisible(busy);
	status_->setVisible(busy);
	FitCardHeight(this, kWidth);
}

void UpdateDialog::OnUpdateClicked()
{
	if (!controller_.CanInstallUpdate()) {
		ShowMessage(this, Text(controller_.CurrentLiveSource() != LiveSource::None ? "Update.LiveBlocked"
											   : "Update.DownloadFailed"));
		return;
	}

	progress_->setValue(0);
	status_->setText(Text("Update.Downloading"));
	SetBusy(true);

	QPointer<UpdateDialog> self = this;
	controller_.DownloadUpdate(
		[self](int percent) {
			if (self)
				self->progress_->setValue(percent);
		},
		[self](InstallerDownload result) {
			if (!self)
				return;
			if (result != InstallerDownload::Ok) {
				self->SetBusy(false);
				ShowMessage(self, Text(result == InstallerDownload::ChecksumMismatch
							       ? "Update.ChecksumMismatch"
							       : "Update.DownloadFailed"));
				return;
			}
			ShowMessage(self, Text("Update.Installing"));
			if (!self->controller_.LaunchUpdateInstaller()) {
				self->SetBusy(false);
				ShowMessage(self, Text("Update.LaunchFailed"));
				return;
			}
			self->accept();
			emit self->QuitRequested();
		});
}

} // namespace langlive::obsplugin::ui
