#include "obs/start-failure-watcher.hpp"

#include <doctest/doctest.h>

#include <QObject>

using langlive::obsplugin::obs::StartFailureWatcher;

namespace {

// Declares the signal like OBS 31's main window (OBSBasic).
class FakeMainWindow : public QObject {
	Q_OBJECT

signals:
	void StreamingStopped(bool withDelay = false);
};

// A main window without the signal, like a future OBS that renamed it.
class OtherWindow : public QObject {
	Q_OBJECT
};

} // namespace

TEST_CASE("start failure watcher: reports OBS's StreamingStopped signal")
{
	FakeMainWindow window;
	int calls = 0;
	StartFailureWatcher watcher(&window, [&]() { ++calls; });
	CHECK(watcher.IsWatching());

	emit window.StreamingStopped();
	CHECK(calls == 1); // direct: before OBS shows its error dialog
	emit window.StreamingStopped(true);
	CHECK(calls == 2);
}

TEST_CASE("start failure watcher: an OBS without the signal is reported, not a crash")
{
	OtherWindow window;
	StartFailureWatcher watcher(&window, []() {});
	CHECK_FALSE(watcher.IsWatching());
	StartFailureWatcher none(nullptr, []() {});
	CHECK_FALSE(none.IsWatching());
}

#include "start_failure_watcher_test.moc"
