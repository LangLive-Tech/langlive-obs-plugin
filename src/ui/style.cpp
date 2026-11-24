#include "ui/style.hpp"

#include <QColor>
#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QWidget>

#include <utility>

namespace langlive::obsplugin::ui {

namespace {

constexpr const char *kSharedStyleSheet = R"(
	QDialog { background-color: {surface}; color: {text}; }
	QLabel { color: {text}; background: transparent; }
	QLabel#sectionTitle { font-size: 15px; font-weight: 600; }
	QLabel#fieldLabel { font-size: 13px; font-weight: 600; }
	QLabel#hintLabel { color: {secondaryText}; font-size: 12px; }
	QLabel#errorLabel { color: {error}; font-size: 12px; }
	QLabel:disabled, QCheckBox:disabled, QRadioButton:disabled { color: {secondaryText}; }
	QFrame#divider { border: none; max-height: 1px; min-height: 1px; background-color: {border}; }

	QLineEdit, QComboBox, QPlainTextEdit {
		background-color: {header};
		color: {text};
		border: 1px solid {border};
		border-radius: 6px;
		padding: 0 10px;
		font-size: 14px;
		selection-background-color: {brand};
	}
	QLineEdit, QComboBox { min-height: 34px; }
	QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: {brand}; }
	QLineEdit:disabled, QComboBox:disabled, QPlainTextEdit:disabled {
		color: {secondaryText};
		background-color: #2a2a2a;
		border-color: #3c3c3c;
	}
	QComboBox::drop-down { border: none; width: 26px; }
	QComboBox::down-arrow { image: url(:/langlive-obs-plugin/chevron-down.svg); width: 12px; height: 8px; }
	QComboBox::down-arrow:on { image: url(:/langlive-obs-plugin/chevron-up.svg); }
	QComboBox QAbstractItemView {
		background-color: {header};
		color: {text};
		border: 1px solid {border};
		selection-background-color: {brand};
		outline: none;
	}

	QCheckBox, QRadioButton { color: {text}; spacing: 8px; }

	/* Scenario options as selectable cards */
	QRadioButton#scenarioCard {
		border: 1px solid {border};
		border-radius: 6px;
		padding: 10px 12px;
		font-size: 14px;
	}
	QRadioButton#scenarioCard:checked { border: 1px solid {brand}; background-color: rgba(109, 100, 195, 0.18); }
	QRadioButton#scenarioCard:disabled { color: {secondaryText}; }
	QRadioButton#scenarioCard:checked:disabled { border-color: #5a5675; background-color: rgba(109, 100, 195, 0.08); }

	/* OBS's theme gives every QPushButton a top margin and a height cap for
	   its own rows; ours line up with the fields beside them instead. */
	QPushButton#primaryButton, QPushButton#secondaryButton { margin: 0; }
	QPushButton#primaryButton {
		background-color: {brand};
		color: white;
		border: none;
		border-radius: 19px;
		min-height: 38px;
		max-height: 38px;
		padding: 0 28px;
		font-size: 15px;
	}
	QPushButton#primaryButton:hover { background-color: {brandHover}; }
	QPushButton#primaryButton:pressed { background-color: {brandPressed}; }
	QPushButton#primaryButton:disabled { background-color: {border}; color: {secondaryText}; }
	QPushButton#secondaryButton {
		background: transparent;
		color: {text};
		border: 1px solid {border};
		border-radius: 18px;
		min-height: 36px;
		max-height: 36px;
		padding: 0 20px;
		font-size: 14px;
	}
	QPushButton#secondaryButton:hover { border-color: {brand}; color: {brandHover}; }
	QPushButton#secondaryButton:pressed { border-color: {brandPressed}; color: {brandPressed}; }
	QPushButton#secondaryButton:disabled { color: {secondaryText}; border-color: {border}; }
	/* Beside a text field (MatchFieldHeight): as tall as the field. */
	QPushButton#secondaryButton[fieldHeight="true"] { min-height: 34px; max-height: 34px; }

	QSlider::groove:horizontal { height: 4px; background: {border}; border-radius: 2px; }
	QSlider::sub-page:horizontal { background: {brand}; border-radius: 2px; }
	QSlider::handle:horizontal { background: {text}; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }

	/* Card dialogs (MakeCard) */
	QDialog[card="true"] { background: transparent; }
	QFrame#card { background-color: {surface}; border: 1px solid {border}; border-radius: 12px; }
	QFrame#cardHeader {
		background-color: {header};
		border: none;
		border-top-left-radius: 12px;
		border-top-right-radius: 12px;
	}
	QLabel#cardTitle { font-size: 18px; font-weight: 600; }
	QPushButton#closeButton {
		background: transparent;
		color: {secondaryText};
		border: none;
		font-size: 22px;
		min-width: 28px;
		min-height: 28px;
	}
	QPushButton#closeButton:hover { color: {text}; }

	QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
	QScrollBar::handle:vertical { background: {border}; border-radius: 3px; min-height: 30px; }
	QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
	QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)";

QString ExpandTokens(QString styleSheet)
{
	static const std::pair<const char *, const char *> kTokens[] = {
		{"{brand}", kBrandColor},
		{"{brandHover}", kBrandHoverColor},
		{"{brandPressed}", kBrandPressedColor},
		{"{surface}", kSurfaceColor},
		{"{header}", kHeaderColor},
		{"{tabBar}", kTabBarColor},
		{"{border}", kBorderColor},
		{"{text}", kTextColor},
		{"{secondaryText}", kSecondaryTextColor},
		{"{error}", kErrorColor},
		{"{warning}", kWarningColor},
	};
	for (const auto &[token, color] : kTokens)
		styleSheet.replace(QString::fromUtf8(token), QString::fromUtf8(color));
	return styleSheet;
}

QColor Color(const char *hex)
{
	return QColor(QString::fromUtf8(hex));
}

} // namespace

void ApplyDialogStyle(QWidget *dialog, const char *extraStyleSheet)
{
	// The palette covers what style sheets cannot reach: placeholder
	// text, custom-painted widgets, and message boxes opened from the
	// dialog.
	QPalette palette = dialog->palette();
	palette.setColor(QPalette::Window, Color(kSurfaceColor));
	palette.setColor(QPalette::WindowText, Color(kTextColor));
	palette.setColor(QPalette::Base, Color(kHeaderColor));
	palette.setColor(QPalette::AlternateBase, Color(kSurfaceColor));
	palette.setColor(QPalette::Text, Color(kTextColor));
	palette.setColor(QPalette::PlaceholderText, Color(kSecondaryTextColor));
	palette.setColor(QPalette::Button, Color(kHeaderColor));
	palette.setColor(QPalette::ButtonText, Color(kTextColor));
	palette.setColor(QPalette::Highlight, Color(kBrandColor));
	palette.setColor(QPalette::HighlightedText, Qt::white);
	palette.setColor(QPalette::Link, Color(kBrandColor));
	palette.setColor(QPalette::Mid, Color(kBorderColor));
	palette.setColor(QPalette::Disabled, QPalette::WindowText, Color(kSecondaryTextColor));
	palette.setColor(QPalette::Disabled, QPalette::Text, Color(kSecondaryTextColor));
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, Color(kSecondaryTextColor));
	dialog->setPalette(palette);

	QString styleSheet = QString::fromUtf8(kSharedStyleSheet);
	if (extraStyleSheet)
		styleSheet += QString::fromUtf8(extraStyleSheet);
	dialog->setStyleSheet(ExpandTokens(styleSheet));
}

Card MakeCard(QDialog *dialog, const QString &title, bool closable)
{
	dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
	dialog->setAttribute(Qt::WA_TranslucentBackground);
	dialog->setWindowTitle(title);
	dialog->setProperty("card", true);
	dialog->style()->unpolish(dialog);
	dialog->style()->polish(dialog);

	auto *outer = new QVBoxLayout(dialog);
	outer->setContentsMargins(0, 0, 0, 0);

	Card card;
	card.frame = new QFrame(dialog);
	card.frame->setObjectName(QStringLiteral("card"));
	outer->addWidget(card.frame);

	card.content = new QVBoxLayout(card.frame);
	card.content->setContentsMargins(1, 1, 1, 1); // inside the card border
	card.content->setSpacing(0);

	auto *header = new QFrame(card.frame);
	header->setObjectName(QStringLiteral("cardHeader"));
	header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	auto *headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(28, 16, 20, 16);
	auto *titleLabel = new QLabel(title, header);
	titleLabel->setObjectName(QStringLiteral("cardTitle"));
	headerLayout->addWidget(titleLabel, 1);
	if (closable) {
		card.closeButton = new QPushButton(QStringLiteral("✕"), header);
		card.closeButton->setObjectName(QStringLiteral("closeButton"));
		card.closeButton->setCursor(Qt::PointingHandCursor);
		card.closeButton->setFocusPolicy(Qt::NoFocus);
		headerLayout->addWidget(card.closeButton);
		QObject::connect(card.closeButton, &QPushButton::clicked, dialog, &QDialog::reject);
	}
	card.content->addWidget(header);
	return card;
}

void FitCardHeight(QDialog *dialog, int width)
{
	dialog->setFixedWidth(width);
	dialog->setFixedHeight(dialog->layout()->totalHeightForWidth(width));
}

QLabel *MakeSectionTitle(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setObjectName(QStringLiteral("sectionTitle"));
	return label;
}

QLabel *MakeFieldLabel(const QString &text, QWidget *parent, bool required)
{
	auto *label = new QLabel(parent);
	label->setObjectName(QStringLiteral("fieldLabel"));
	if (required) {
		label->setTextFormat(Qt::RichText);
		label->setText(QStringLiteral("%1 <span style=\"color:%2;\">*</span>")
				       .arg(text.toHtmlEscaped(), QString::fromUtf8(kErrorColor)));
	} else {
		label->setText(text);
	}
	return label;
}

QLabel *MakeHintLabel(const QString &text, QWidget *parent, Qt::Alignment alignment)
{
	auto *label = new QLabel(text, parent);
	label->setObjectName(QStringLiteral("hintLabel"));
	label->setWordWrap(true);
	label->setAlignment(alignment);
	return label;
}

void MatchFieldHeight(QPushButton *button)
{
	button->setProperty("fieldHeight", true);
	button->setAttribute(Qt::WA_LayoutUsesWidgetRect);
}

QFrame *MakeDivider(QWidget *parent)
{
	auto *divider = new QFrame(parent);
	divider->setObjectName(QStringLiteral("divider"));
	divider->setFrameShape(QFrame::NoFrame);
	return divider;
}

} // namespace langlive::obsplugin::ui
