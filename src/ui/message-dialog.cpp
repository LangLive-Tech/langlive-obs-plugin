#include "ui/message-dialog.hpp"

#include "ui/style.hpp"
#include "ui/text.hpp"

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace langlive::obsplugin::ui {

namespace {

constexpr int kWidth = 320;
constexpr int kConfirmWidth = 400;

// A card like the website's dialogs: rounded, title area on top, centered
// message, one wide button. No system title bar, which would repeat the
// title.
constexpr const char *kStyleSheet = R"(
	QDialog { background: transparent; }
	QFrame#messageCard { background-color: {surface}; border: 1px solid {border}; border-radius: 12px; }
	QFrame#messageHeader {
		background-color: {header};
		border: none;
		border-top-left-radius: 12px;
		border-top-right-radius: 12px;
	}
	QLabel#messageTitle { font-size: 16px; font-weight: 600; }
	QLabel#messageBody { font-size: 14px; }
	QPushButton#primaryButton { min-height: 34px; border-radius: 17px; font-size: 14px; }
)";

// Returns to the window the dialog came from; macOS otherwise brings OBS's
// main window to the front when a frameless dialog closes.
void RaiseAgain(QWidget *parent)
{
	if (!parent)
		return;
	QPointer<QWidget> window = parent->window();
	QTimer::singleShot(0, window, [window]() {
		if (window && window->isVisible()) {
			window->raise();
			window->activateWindow();
		}
	});
}

} // namespace

void ShowMessage(QWidget *parent, const QString &message, const QString &title, const QString &buttonText)
{
	QDialog dialog(parent, Qt::Dialog | Qt::FramelessWindowHint);
	dialog.setAttribute(Qt::WA_TranslucentBackground);
	dialog.setWindowTitle(Text("Brand"));
	dialog.setFixedWidth(kWidth);
	ApplyDialogStyle(&dialog, kStyleSheet);

	auto *outer = new QVBoxLayout(&dialog);
	outer->setContentsMargins(0, 0, 0, 0);

	auto *card = new QFrame(&dialog);
	card->setObjectName(QStringLiteral("messageCard"));
	outer->addWidget(card);

	auto *layout = new QVBoxLayout(card);
	layout->setContentsMargins(1, 1, 1, 1); // inside the card border
	layout->setSpacing(0);

	auto *header = new QFrame(card);
	header->setObjectName(QStringLiteral("messageHeader"));
	header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	auto *headerLayout = new QVBoxLayout(header);
	headerLayout->setContentsMargins(20, 12, 20, 12);
	auto *titleLabel = new QLabel(title.isEmpty() ? Text("Message.Title") : title, header);
	titleLabel->setObjectName(QStringLiteral("messageTitle"));
	titleLabel->setAlignment(Qt::AlignCenter);
	headerLayout->addWidget(titleLabel);
	layout->addWidget(header);

	auto *body = new QVBoxLayout();
	body->setContentsMargins(24, 20, 24, 20);
	body->setSpacing(20);
	auto *messageLabel = new QLabel(message, card);
	messageLabel->setObjectName(QStringLiteral("messageBody"));
	messageLabel->setAlignment(Qt::AlignCenter);
	messageLabel->setWordWrap(true);
	messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	body->addWidget(messageLabel);

	auto *ok = new QPushButton(buttonText.isEmpty() ? Text("Message.OK") : buttonText, card);
	ok->setObjectName(QStringLiteral("primaryButton"));
	ok->setCursor(Qt::PointingHandCursor);
	ok->setDefault(true);
	body->addWidget(ok);
	layout->addLayout(body);

	QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
	ok->setFocus();
	// Wrapped labels report their height for a narrower width; size the
	// dialog for its real width so it has no empty space.
	dialog.setFixedHeight(outer->totalHeightForWidth(kWidth));
	dialog.exec();
	RaiseAgain(parent);
}

bool AskConfirmation(QWidget *parent, const QString &title, const QString &question, const QString &confirmText)
{
	QDialog dialog(parent);
	ApplyDialogStyle(&dialog, "QLabel#confirmQuestion { font-size: 15px; }");
	const Card card = MakeCard(&dialog, title, true);

	auto *body = new QVBoxLayout();
	body->setContentsMargins(28, 22, 28, 22);
	auto *questionLabel = new QLabel(question, card.frame);
	questionLabel->setObjectName(QStringLiteral("confirmQuestion"));
	questionLabel->setWordWrap(true);
	body->addWidget(questionLabel);
	card.content->addLayout(body);

	card.content->addWidget(MakeDivider(card.frame));
	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(28, 14, 28, 16);
	footer->setSpacing(10);
	footer->addStretch();
	auto *cancel = new QPushButton(Text("Message.Cancel"), card.frame);
	cancel->setObjectName(QStringLiteral("secondaryButton"));
	auto *confirm = new QPushButton(confirmText, card.frame);
	confirm->setObjectName(QStringLiteral("primaryButton"));
	for (QPushButton *button : {cancel, confirm}) {
		button->setCursor(Qt::PointingHandCursor);
		button->setAutoDefault(false);
		footer->addWidget(button);
	}
	card.content->addLayout(footer);

	QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
	QObject::connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
	// Enter must not confirm by accident; focus starts on Cancel.
	cancel->setFocus();
	FitCardHeight(&dialog, kConfirmWidth);
	const bool confirmed = dialog.exec() == QDialog::Accepted;
	RaiseAgain(parent);
	return confirmed;
}

} // namespace langlive::obsplugin::ui
