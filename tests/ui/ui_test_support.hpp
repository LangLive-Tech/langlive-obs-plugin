#pragma once

#include "app/fakes.hpp"
#include "app/plugin-controller.hpp"
#include "ui/text.hpp"

#include <doctest/doctest.h>

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include <functional>

namespace langlive::obsplugin::uitest {

using namespace langlive::obsplugin::testing;

inline void ProcessEvents()
{
	QApplication::processEvents();
}

// The visible button with this text, or nullptr.
inline QPushButton *FindButton(const QWidget &root, const QString &text)
{
	for (QPushButton *button : root.findChildren<QPushButton *>())
		if (button->text() == text && button->isVisible())
			return button;
	return nullptr;
}

// All label text under `root`, joined, for "does it say X" checks.
inline QString AllText(const QWidget &root)
{
	QStringList texts;
	for (const QLabel *label : root.findChildren<QLabel *>())
		texts << label->text();
	return texts.join(QLatin1Char('\n'));
}

// Runs `open`, which shows a modal dialog with exec(), inspects that dialog
// once it is up, then closes it unless `inspect` already did (e.g. by
// clicking a button). Returns false if no modal dialog appeared.
inline bool WithModal(const std::function<void()> &open, const std::function<void(QDialog &)> &inspect)
{
	bool seen = false;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, [&]() {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog)
			return;
		seen = true;
		inspect(*dialog);
		if (dialog->isVisible())
			dialog->done(QDialog::Rejected);
	});
	timer.start(50);
	open();
	timer.stop();
	return seen;
}

// A controller with in-memory OBS and storage.
struct ControllerFixture {
	FakeStreamingHost host;
	FakeSettingsStore store;
	PluginController controller{host, store};

	ControllerFixture()
	{
		host.state = ValidState(core::GetStreamSpec(core::Scenario::General, core::Orientation::Landscape));
	}

	void LogIn() { REQUIRE(testing::LogIn(controller) == SmsLoginResponse::Result::Success); }
};

} // namespace langlive::obsplugin::uitest
