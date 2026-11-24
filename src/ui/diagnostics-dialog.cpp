#include "ui/diagnostics-dialog.hpp"

#include "obs/diagnostics.hpp"
#include "ui/message-dialog.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"

#include <algorithm>

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace langlive::obsplugin::ui {

namespace {

constexpr int kDialogWidth = 560;
constexpr int kExportedMaxWidth = 640; // the export result's widest

constexpr const char *kStyleSheet = R"(
	QFrame#contentsCard {
		border: 1px solid {border};
		border-radius: 6px;
		background-color: {header};
	}
	QLabel#bullet { font-size: 14px; }
	QLabel#exportedTitle { font-size: 18px; font-weight: 600; }
	QLabel#exportedIcon {
		background-color: #2e9e5b;
		color: white;
		border-radius: 22px;
		font-size: 24px;
		font-weight: 700;
	}
	QLineEdit:read-only { color: {text}; }
)";

// The three kinds of content in the export, as a bordered list.
QFrame *MakeContentsCard(QWidget *parent)
{
	auto *card = new QFrame(parent);
	card->setObjectName(QStringLiteral("contentsCard"));
	auto *layout = new QVBoxLayout(card);
	layout->setContentsMargins(16, 12, 16, 12);
	layout->setSpacing(6);
	for (const char *key : {"Diag.ContentLogs", "Diag.ContentCrashes", "Diag.ContentSettings"}) {
		auto *item = new QLabel(QStringLiteral("•  %1").arg(Text(key)), card);
		item->setObjectName(QStringLiteral("bullet"));
		layout->addWidget(item);
	}
	return card;
}

// Footer with the actions, separated from the content by a divider.
QHBoxLayout *AddFooter(QVBoxLayout *outer, QWidget *parent)
{
	outer->addWidget(MakeDivider(parent));
	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(28, 14, 28, 14);
	footer->setSpacing(10);
	footer->addStretch();
	outer->addLayout(footer);
	return footer;
}

QPushButton *MakeButton(const QString &text, const char *objectName, QWidget *parent)
{
	auto *button = new QPushButton(text, parent);
	button->setObjectName(QString::fromUtf8(objectName));
	button->setCursor(Qt::PointingHandCursor);
	return button;
}

} // namespace

DiagnosticsDialog::DiagnosticsDialog(QWidget *parent) : QDialog(parent), directory_(obs::DefaultDiagnosticsDir())
{
	setWindowTitle(Text("Diag.Title"));
	setFixedWidth(kDialogWidth);
	ApplyDialogStyle(this, kStyleSheet);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	auto *content = new QVBoxLayout();
	content->setContentsMargins(28, 24, 28, 24);
	content->setSpacing(20);
	outer->addLayout(content);

	auto *description = new QLabel(Text("Diag.Description"), this);
	description->setWordWrap(true);
	content->addWidget(description);

	auto *contentsBlock = new QVBoxLayout();
	contentsBlock->setSpacing(8);
	contentsBlock->addWidget(MakeFieldLabel(Text("Diag.ContentsTitle"), this));
	contentsBlock->addWidget(MakeContentsCard(this));
	contentsBlock->addWidget(MakeHintLabel(Text("Diag.LocalOnly"), this));
	content->addLayout(contentsBlock);

	auto *locationBlock = new QVBoxLayout();
	locationBlock->setSpacing(8);
	locationBlock->addWidget(MakeFieldLabel(Text("Diag.Location"), this));
	auto *locationRow = new QHBoxLayout();
	locationRow->setSpacing(10);
	directoryEdit_ = new QLineEdit(QDir::toNativeSeparators(directory_), this);
	directoryEdit_->setReadOnly(true);
	directoryEdit_->setFocusPolicy(Qt::NoFocus); // display only; "Change Location" edits it
	changeButton_ = MakeButton(Text("Diag.ChangeLocation"), "secondaryButton", this);
	MatchFieldHeight(changeButton_);
	directoryEdit_->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	// Centered like the login dialog's code row (OBS's theme would put the
	// button lower).
	locationRow->addWidget(directoryEdit_, 1, Qt::AlignVCenter);
	locationRow->addWidget(changeButton_, 0, Qt::AlignVCenter);
	locationBlock->addLayout(locationRow);
	fileNameLabel_ = MakeHintLabel(Text("Diag.FileName").arg(obs::DiagnosticsFileName()), this);
	locationBlock->addWidget(fileNameLabel_);
	content->addLayout(locationBlock);

	QHBoxLayout *footer = AddFooter(outer, this);
	cancelButton_ = MakeButton(Text("Diag.Cancel"), "secondaryButton", this);
	exportButton_ = MakeButton(Text("Diag.Export"), "primaryButton", this);
	exportButton_->setDefault(true);
	footer->addWidget(cancelButton_);
	footer->addWidget(exportButton_);

	connect(changeButton_, &QPushButton::clicked, this, &DiagnosticsDialog::OnChangeLocationClicked);
	connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
	connect(exportButton_, &QPushButton::clicked, this, &DiagnosticsDialog::OnExportClicked);
}

void DiagnosticsDialog::OnChangeLocationClicked()
{
	const QString directory = QFileDialog::getExistingDirectory(this, Text("Diag.ChangeLocation"), directory_);
	if (directory.isEmpty())
		return;
	directory_ = directory;
	directoryEdit_->setText(QDir::toNativeSeparators(directory_));
}

void DiagnosticsDialog::OnExportClicked()
{
	const QString zipPath = QDir(directory_).filePath(obs::DiagnosticsFileName());

	// Exporting blocks for a moment; show it and keep the dialog inert.
	for (QWidget *widget : std::initializer_list<QWidget *>{changeButton_, cancelButton_, exportButton_})
		widget->setEnabled(false);
	exportButton_->setText(Text("Diag.Exporting"));
	QApplication::setOverrideCursor(Qt::WaitCursor);
	QApplication::processEvents();

	const obs::ExportResult result = obs::ExportDiagnostics(zipPath);

	QApplication::restoreOverrideCursor();
	for (QWidget *widget : std::initializer_list<QWidget *>{changeButton_, cancelButton_, exportButton_})
		widget->setEnabled(true);
	exportButton_->setText(Text("Diag.Export"));

	if (!result.success) {
		ShowMessage(this, Text("Diag.Failed").arg(result.error));
		return;
	}
	ShowExported(zipPath);
}

void DiagnosticsDialog::ShowExported(const QString &zipPath)
{
	DiagnosticsExportedDialog exported(zipPath, this);
	exported.exec();
	accept();
}

DiagnosticsExportedDialog::DiagnosticsExportedDialog(const QString &zipPath, QWidget *parent) : QDialog(parent)
{
	setWindowTitle(Text("Diag.Title"));
	setFixedWidth(kDialogWidth - 80);
	ApplyDialogStyle(this, kStyleSheet);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	auto *content = new QVBoxLayout();
	content->setContentsMargins(28, 28, 28, 24);
	content->setSpacing(12);
	outer->addLayout(content);

	auto *icon = new QLabel(QStringLiteral("✓"), this);
	icon->setObjectName(QStringLiteral("exportedIcon"));
	icon->setAlignment(Qt::AlignCenter);
	icon->setFixedSize(44, 44);
	content->addWidget(icon, 0, Qt::AlignHCenter);

	auto *title = new QLabel(Text("Diag.ExportedTitle"), this);
	title->setObjectName(QStringLiteral("exportedTitle"));
	title->setAlignment(Qt::AlignCenter);
	content->addWidget(title);

	auto *message = new QLabel(Text("Diag.Exported"), this);
	message->setWordWrap(true);
	message->setAlignment(Qt::AlignCenter);
	content->addWidget(message);
	// Wide enough for the message on one line (the Chinese one is a few
	// characters too long for the default width); a longer translation
	// wraps rather than making the dialog too wide. Polished first: the
	// style sheet sets the label's font.
	message->ensurePolished();
	const int oneLine = message->fontMetrics().horizontalAdvance(message->text()) +
			    content->contentsMargins().left() + content->contentsMargins().right() + 8;
	setFixedWidth(std::clamp(oneLine, kDialogWidth - 80, kExportedMaxWidth));

	// Long paths are shortened in the middle; the tooltip has the full path.
	const QString nativePath = QDir::toNativeSeparators(zipPath);
	auto *path = MakeHintLabel(QString(), this, Qt::AlignCenter);
	path->setWordWrap(false);
	path->setText(path->fontMetrics().elidedText(nativePath, Qt::ElideMiddle, width() - 56));
	path->setToolTip(nativePath);
	content->addWidget(path);

	QHBoxLayout *footer = AddFooter(outer, this);
	QPushButton *openFolder = MakeButton(Text("Diag.OpenFolder"), "secondaryButton", this);
	QPushButton *done = MakeButton(Text("Diag.Done"), "primaryButton", this);
	done->setDefault(true);
	footer->addWidget(openFolder);
	footer->addWidget(done);

	connect(openFolder, &QPushButton::clicked, this, [this, zipPath]() {
		QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(zipPath).absolutePath()));
		accept();
	});
	connect(done, &QPushButton::clicked, this, &QDialog::accept);
}

} // namespace langlive::obsplugin::ui
