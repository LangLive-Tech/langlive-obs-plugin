#pragma once

#include <QString>

class QWidget;

namespace langlive::obsplugin::ui {

// A notice in the plugin's style: title, message, and an OK button. Use
// it instead of QMessageBox, which follows the system look. Empty `title`
// and `buttonText` mean "Notice" and "OK".
void ShowMessage(QWidget *parent, const QString &message, const QString &title = QString(),
		 const QString &buttonText = QString());

// Asks the streamer to confirm an action: title, question, Cancel and a
// button named after the action. Returns true when the action is confirmed;
// Cancel, the close button and Escape return false.
bool AskConfirmation(QWidget *parent, const QString &title, const QString &question, const QString &confirmText);

} // namespace langlive::obsplugin::ui
