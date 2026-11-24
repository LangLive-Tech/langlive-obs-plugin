#pragma once

#include <QString>
#include <qnamespace.h>

class QDialog;
class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace langlive::obsplugin::ui {

// LangLive website colors. The plugin's dialogs use this dark theme on
// every OBS theme, so they look like the website's dialogs.
inline constexpr const char *kBrandColor = "#6d64c3";
inline constexpr const char *kBrandHoverColor = "#7f77d1";
inline constexpr const char *kBrandPressedColor = "#5a52a8";
inline constexpr const char *kSurfaceColor = "#313131"; // dialog body
inline constexpr const char *kHeaderColor = "#1d1d1d";  // dialog title area, inputs
inline constexpr const char *kTabBarColor = "#0a0a0a";  // tab bar
inline constexpr const char *kBorderColor = "#4a4a4a";
inline constexpr const char *kTextColor = "#ffffff";
inline constexpr const char *kSecondaryTextColor = "#8c8c94";
inline constexpr const char *kErrorColor = "#ff6b6b";
inline constexpr const char *kWarningColor = "#f0a03c";

// Applies the dark theme to a dialog: palette plus the shared style sheet,
// followed by `extraStyleSheet`. Both style sheets may use the color
// tokens {brand}, {brandHover}, {brandPressed}, {surface}, {header},
// {tabBar}, {border}, {text}, {secondaryText}, {error}, {warning}.
//
// Widgets opt in to shared styles through their object names:
// primaryButton, secondaryButton, sectionTitle, fieldLabel, hintLabel,
// errorLabel, divider, scenarioCard (QRadioButton).
void ApplyDialogStyle(QWidget *dialog, const char *extraStyleSheet = nullptr);

// A frameless, rounded card with a title area, like the website's dialogs.
// Call after ApplyDialogStyle.
struct Card {
	QFrame *frame = nullptr;            // parent for the card's widgets
	QVBoxLayout *content = nullptr;     // add the body and footer here
	QPushButton *closeButton = nullptr; // set when closable; rejects the dialog
};
Card MakeCard(QDialog *dialog, const QString &title, bool closable);

// Sizes a card dialog's height for `width`. Wrapped labels report their
// height for a narrower width, which would leave empty space.
void FitCardHeight(QDialog *dialog, int width);

QLabel *MakeSectionTitle(const QString &text, QWidget *parent);
QLabel *MakeFieldLabel(const QString &text, QWidget *parent, bool required = false);
// Secondary text.
QLabel *MakeHintLabel(const QString &text, QWidget *parent, Qt::Alignment alignment = Qt::AlignLeft);
QFrame *MakeDivider(QWidget *parent);
// A secondary button in a row with a text field: the field's height, and
// laid out by its own rect so the two line up.
void MatchFieldHeight(QPushButton *button);

} // namespace langlive::obsplugin::ui
