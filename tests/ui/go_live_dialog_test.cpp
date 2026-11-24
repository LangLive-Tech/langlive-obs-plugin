#include "ui_test_support.hpp"

#include "ui/cover-picker.hpp"
#include "ui/go-live-dialog.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <vector>

#include <QBuffer>
#include <QComboBox>
#include <QFile>
#include <QPointer>
#include <QFileInfo>
#include <QApplication>
#include <QFileDialog>
#include <QMouseEvent>
#include <QTimer>
#include <QTemporaryDir>
#include <QImage>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QLineEdit>
#include <QRadioButton>
#include <QStyleHints>
#include <QStandardPaths>

using namespace langlive::obsplugin;
using namespace langlive::obsplugin::uitest;
using ui::Text;

namespace {

QLineEdit *TitleEdit(const QWidget &dialog)
{
	for (QLineEdit *edit : dialog.findChildren<QLineEdit *>())
		if (edit->placeholderText() == Text("GoLive.StreamTitlePlaceholder"))
			return edit;
	return nullptr;
}

QComboBox *ChannelCombo(const QWidget &dialog)
{
	for (QComboBox *combo : dialog.findChildren<QComboBox *>())
		if (combo->itemText(0) == Text("GoLive.ChannelPlaceholder"))
			return combo;
	return nullptr;
}

struct GoLiveFixture : ControllerFixture {
	GoLiveFixture() { LogIn(); }

	void FillRequiredFields(ui::GoLiveDialog &dialog)
	{
		REQUIRE(TitleEdit(dialog));
		REQUIRE(ChannelCombo(dialog));
		TitleEdit(dialog)->setText(QStringLiteral("Tonight"));
		ChannelCombo(dialog)->setCurrentIndex(1);
	}
};

} // namespace

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: missing fields are listed in one notice")
{
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	ProcessEvents();

	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.Start"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("GoLive.MissingTitle")));
	CHECK(notice.contains(Text("GoLive.MissingChannel")));
	CHECK(host.startCount == 0);
	CHECK(dialog.isVisible());
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: closes once the live has started, not before")
{
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);

	FindButton(dialog, Text("GoLive.Start"))->click();
	CHECK(host.startCount == 1);
	CHECK(dialog.isVisible()); // still waiting for OBS

	REQUIRE(controller.OnStreamingStarted().empty());
	dialog.Refresh();
	CHECK_FALSE(dialog.isVisible());
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: stays open when the pre-stream check stops the stream")
{
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);

	FindButton(dialog, Text("GoLive.Start"))->click();
	host.state.videoCodec = "hevc"; // OBS's encoder differs from what the profile said
	CHECK_FALSE(controller.OnStreamingStarted().empty());
	dialog.CancelPendingClose();
	dialog.Refresh();
	CHECK(dialog.isVisible());
	CHECK(dialog.windowTitle() == Text("GoLive.Title"));
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: locked while live, End Live only")
{
	GoLiveRequest request;
	request.settings.title = "Tonight";
	request.channelId = "channel-1";
	REQUIRE(testing::StartLive(controller, request).started());
	REQUIRE(controller.OnStreamingStarted().empty());

	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	ProcessEvents();

	CHECK(dialog.windowTitle() == Text("GoLive.TitleLive"));
	CHECK(AllText(dialog).contains(Text("GoLive.LiveNotice")));
	CHECK_FALSE(TitleEdit(dialog)->isEnabled());
	CHECK_FALSE(ChannelCombo(dialog)->isEnabled());
	CHECK_FALSE(FindButton(dialog, Text("GoLive.Save"))->isEnabled());
	REQUIRE(FindButton(dialog, Text("GoLive.End")));

	SUBCASE("reconnecting")
	{
		controller.OnStreamReconnecting();
		dialog.Refresh();
		CHECK(dialog.windowTitle() == Text("GoLive.TitleReconnecting"));
		CHECK(AllText(dialog).contains(Text("GoLive.ReconnectingNotice")));
		CHECK(FindButton(dialog, Text("GoLive.End"))->isEnabled());

		controller.OnStreamReconnected();
		dialog.Refresh();
		CHECK(dialog.windowTitle() == Text("GoLive.TitleLive"));
	}
	SUBCASE("End Live asks first; Cancel keeps the live")
	{
		CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.End"))->click(); },
				[&](QDialog &modal) {
					CHECK(AllText(modal).contains(Text("EndLive.Question")));
					FindButton(modal, Text("Message.Cancel"))->click();
				}));
		CHECK(host.stopCount == 0);
		CHECK(controller.CurrentLiveSource() == LiveSource::Plugin);
	}
	SUBCASE("ended")
	{
		CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.End"))->click(); },
				[&](QDialog &modal) { FindButton(modal, Text("EndLive.Confirm"))->click(); }));
		CHECK(host.stopCount == 1);
		controller.OnStreamingStopped();
		dialog.Refresh();
		CHECK(dialog.windowTitle() == Text("GoLive.Title"));
		CHECK(TitleEdit(dialog)->isEnabled());
		CHECK(FindButton(dialog, Text("GoLive.Save"))->isEnabled());
	}
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: Tab goes down the form, also where Tab skips all but text fields")
{
	// macOS's default (Keyboard navigation off).
	QStyleHints *hints = QGuiApplication::styleHints();
	const Qt::TabFocusBehavior before = hints->tabFocusBehavior();
	hints->setTabFocusBehavior(Qt::TabFocusTextControls);

	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	dialog.activateWindow();
	QApplication::processEvents();

	const QList<QLineEdit *> edits = dialog.findChildren<QLineEdit *>();
	const QList<QComboBox *> combos = dialog.findChildren<QComboBox *>();
	const QList<QRadioButton *> radios = dialog.findChildren<QRadioButton *>();
	REQUIRE(edits.size() == 2);
	REQUIRE(combos.size() == 3);
	REQUIRE(radios.size() == 2);
	REQUIRE(radios[0]->isChecked()); // General
	const std::vector<QWidget *> expected = {
		edits[0],  // title (focused when the dialog opens)
		edits[1],  // push text
		combos[0], // main event
		combos[1], // sub event
		combos[2], // channel
		radios[0], // the checked scenario; arrow keys pick the other
		FindButton(dialog, Text("GoLive.Save")),
		FindButton(dialog, Text("GoLive.Start")),
		FindButton(dialog, Text("GoLive.UploadCover")),
		edits[0], // around again
	};
	std::vector<QWidget *> visited;
	for (size_t i = 0; i < expected.size(); ++i) {
		QWidget *focused = QApplication::focusWidget();
		visited.push_back(focused);
		if (!focused)
			break;
		QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
		QApplication::sendEvent(focused, &tab);
	}
	CHECK(visited == expected);
	hints->setTabFocusBehavior(before);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: Escape hides the dialog and keeps the form")
{
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);

	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QApplication::sendEvent(&dialog, &escape);
	CHECK_FALSE(dialog.isVisible());

	dialog.show();
	CHECK(TitleEdit(dialog)->text() == QStringLiteral("Tonight"));
	CHECK(ChannelCombo(dialog)->currentIndex() == 1);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: Save stores the form and applies the stream settings")
{
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);

	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.Save"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("GoLive.Saved")));
	CHECK(store.stored.title == "Tonight");
	CHECK(host.appliedSpec.has_value());
}

namespace {

// Runs `open`, then handles the modal dialogs it opens, in order: each step
// gets the next one and must close it. True if every step ran.
bool WithModals(const std::function<void()> &open, std::vector<std::function<void(QDialog &)>> steps)
{
	size_t next = 0;
	QDialog *last = nullptr;
	QTimer timer;
	timer.setInterval(50);
	QObject::connect(&timer, &QTimer::timeout, [&]() {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if (!dialog || dialog == last || next >= steps.size())
			return;
		last = dialog;
		// Not inside this timer's handler: a step that opens another modal
		// runs its event loop, and Qt does not call a timer's handler again
		// while it is still running.
		QPointer<QDialog> target = dialog;
		const size_t step = next++;
		QTimer::singleShot(0, [&steps, target, step]() {
			if (target)
				steps[step](*target);
		});
	});
	timer.start();
	open();
	timer.stop();
	return next == steps.size();
}

// A temp folder for the form's cover files, removed afterwards.
struct CoverFolder {
	QTemporaryDir dir;
	QString Path(const char *name) const { return dir.filePath(QString::fromLatin1(name)); }
};

QImage Filled(Qt::GlobalColor color)
{
	QImage image(100, 100, QImage::Format_RGB32);
	image.fill(color);
	return image;
}

// Upload Cover, picking `source` in the file dialog, then the crop dialog's
// Upload Cover; `more` handles what opens after that (e.g. an error notice).
bool UploadCoverFromForm(ui::GoLiveDialog &dialog, const QString &source,
			 std::vector<std::function<void(QDialog &)>> more = {})
{
	std::vector<std::function<void(QDialog &)>> steps = {
		[source](QDialog &modal) {
			auto *chooser = qobject_cast<QFileDialog *>(&modal);
			REQUIRE(chooser);
			// As if typed: Qt's own file dialog (offscreen has no native one)
			// takes the name from its file name field when accepted.
			auto *name = chooser->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
			REQUIRE(name);
			name->setText(source);
			modal.accept(); // QFileDialog::accept, public through QDialog
		},
		[](QDialog &modal) {
			REQUIRE(qobject_cast<ui::CoverCropDialog *>(&modal));
			FindButton(modal, Text("Cover.Upload"))->click();
		},
	};
	steps.insert(steps.end(), more.begin(), more.end());
	return WithModals([&]() { FindButton(dialog, Text("GoLive.UploadCover"))->click(); }, std::move(steps));
}

} // namespace

namespace {

// `image` encoded as PNG, as a cover download returns it.
std::string PngBytes(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes.toStdString();
}

bool IsMostly(const QImage &image, Qt::GlobalColor color)
{
	if (image.isNull())
		return false;
	const QColor want(color);
	const QColor got = image.pixelColor(image.width() / 2, image.height() / 2);
	return std::abs(got.red() - want.red()) < 40 && std::abs(got.green() - want.green()) < 40 &&
	       std::abs(got.blue() - want.blue()) < 40;
}

} // namespace

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: the room's cover is downloaded every time the form loads")
{
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	api.info.coverUrl = "https://img.example.com/room.jpg";
	api.imageBytes = PngBytes(Filled(Qt::darkRed));
	controller.SetApi(&api);

	ui::GoLiveDialog dialog(controller, nullptr);
	auto *picker = dialog.findChild<ui::CoverPicker *>();
	REQUIRE(picker);
	CHECK(api.lastImageUrl == "https://img.example.com/room.jpg");
	CHECK(IsMostly(picker->Image(), Qt::darkRed));

	// Changed on the website: the next load shows it.
	api.info.coverUrl = "https://img.example.com/website.jpg";
	api.imageBytes = PngBytes(Filled(Qt::darkCyan));
	dialog.ReloadInfo();
	CHECK(api.lastImageUrl == "https://img.example.com/website.jpg");
	CHECK(IsMostly(picker->Image(), Qt::darkCyan));

	// Not downloadable, or no cover: "No cover uploaded".
	api.imageError = {ApiError::Kind::Network, "", "HTTP 404"};
	dialog.ReloadInfo();
	CHECK(picker->Image().isNull());
	api.imageError = {};
	api.info.coverUrl.clear();
	dialog.ReloadInfo();
	CHECK(picker->Image().isNull());
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: Upload Cover uploads from the crop dialog, and the form shows it")
{
	CoverFolder folder;
	REQUIRE(Filled(Qt::darkCyan).save(folder.Path("source.png")));
	store.coverImagePath = folder.Path("cover-upload.jpg").toStdString();
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	controller.SetApi(&api);

	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	auto *picker = dialog.findChild<ui::CoverPicker *>();
	REQUIRE(picker);
	CHECK(picker->Image().isNull()); // the room has none: "No cover uploaded"

	api.calls.clear();
	CHECK(UploadCoverFromForm(dialog, folder.Path("source.png")));
	CHECK(api.calls == std::vector<std::string>{"cover_upload_url", "upload"});
	CHECK(IsMostly(picker->Image(), Qt::darkCyan));
	CHECK(store.stored.uploadedCoverUrl == "https://img.example.com/c.jpg");
	CHECK_FALSE(QFile::exists(folder.Path("cover-upload.jpg"))); // only for the upload

	// Start Live uses it without uploading again.
	api.calls.clear();
	FillRequiredFields(dialog);
	FindButton(dialog, Text("GoLive.Start"))->click();
	CHECK(api.calls == std::vector<std::string>{"info", "setting", "start"});
	CHECK(api.lastStart.coverUrl == "https://img.example.com/c.jpg");
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: a refused cover upload says why, and the cover stays as it was")
{
	CoverFolder folder;
	REQUIRE(Filled(Qt::darkCyan).save(folder.Path("source.png")));
	store.coverImagePath = folder.Path("cover-upload.jpg").toStdString();
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	api.info.coverUrl = "https://img.example.com/room.jpg";
	api.imageBytes = PngBytes(Filled(Qt::darkRed));
	api.coverUploadError = {ApiError::Kind::Other, "-1", "Daily upload limit reached"};
	controller.SetApi(&api);

	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	auto *picker = dialog.findChild<ui::CoverPicker *>();
	REQUIRE(picker);
	REQUIRE(IsMostly(picker->Image(), Qt::darkRed));

	QString notice;
	CHECK(UploadCoverFromForm(dialog, folder.Path("source.png"), {[&](QDialog &modal) {
					  notice = AllText(modal);
					  modal.done(QDialog::Rejected);
				  }}));
	CHECK(notice.contains(QStringLiteral("Daily upload limit reached")));
	CHECK(IsMostly(picker->Image(), Qt::darkRed)); // the room's cover still
	CHECK(store.stored.uploadedCoverUrl.empty());
	CHECK_FALSE(QFile::exists(folder.Path("cover-upload.jpg")));
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: clicking the cover does nothing")
{
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	auto *picker = dialog.findChild<ui::CoverPicker *>();
	REQUIRE(picker);
	const QPointF center(picker->width() / 2.0, picker->height() / 2.0);
	CHECK_FALSE(WithModal(
		[&]() {
			QMouseEvent press(QEvent::MouseButtonPress, center, picker->mapToGlobal(center), Qt::LeftButton,
					  Qt::LeftButton, Qt::NoModifier);
			QMouseEvent release(QEvent::MouseButtonRelease, center, picker->mapToGlobal(center),
					    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
			QApplication::sendEvent(picker, &press);
			QApplication::sendEvent(picker, &release);
		},
		[](QDialog &) {}));
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: the title saved on the backend fills the form, the channel does not")
{
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}, {"85", "Music"}};
	api.info.title = "Saved title";
	api.info.channelId = "85"; // kept by the backend, but not saved per spec 4.3
	controller.SetApi(&api);

	ui::GoLiveDialog dialog(controller, nullptr);
	CHECK(TitleEdit(dialog)->text() == QStringLiteral("Saved title"));
	REQUIRE(ChannelCombo(dialog)->count() == 3);
	CHECK(ChannelCombo(dialog)->currentIndex() == 0); // the placeholder

	// A reload keeps what the streamer changed since.
	TitleEdit(dialog)->setText(QStringLiteral("Mine"));
	ChannelCombo(dialog)->setCurrentIndex(1);
	dialog.ReloadInfo();
	CHECK(TitleEdit(dialog)->text() == QStringLiteral("Mine"));
	CHECK(ChannelCombo(dialog)->currentText() == QStringLiteral("Chat"));
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: while streaming/info loads, a spinner shows and the form is locked")
{
	FakeLangLiveApi api;
	api.holdInfo = true;
	api.info.channels = {{"84", "Chat"}};
	api.info.title = "Saved title";
	controller.SetApi(&api);

	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	const QLabel *notice = nullptr;
	for (const QLabel *label : dialog.findChildren<QLabel *>())
		if (label->text() == Text("GoLive.Loading"))
			notice = label;
	REQUIRE(notice);
	CHECK(notice->isVisible());
	CHECK_FALSE(TitleEdit(dialog)->isEnabled());
	CHECK_FALSE(ChannelCombo(dialog)->isEnabled());
	CHECK_FALSE(FindButton(dialog, Text("GoLive.UploadCover"))->isEnabled());
	CHECK_FALSE(FindButton(dialog, Text("GoLive.Save"))->isEnabled());
	CHECK_FALSE(FindButton(dialog, Text("GoLive.Start"))->isEnabled());

	api.AnswerHeldInfo();
	CHECK_FALSE(notice->isVisible());
	CHECK(TitleEdit(dialog)->isEnabled());
	CHECK(TitleEdit(dialog)->text() == QStringLiteral("Saved title"));
	CHECK(ChannelCombo(dialog)->isEnabled());
	CHECK(FindButton(dialog, Text("GoLive.Save"))->isEnabled());
	CHECK(FindButton(dialog, Text("GoLive.Start"))->isEnabled());
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: a live running on the website blocks Start Live")
{
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	controller.SetApi(&api);
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);

	api.streamingError = {ApiError::Kind::WebLiveActive, "99304", ""};
	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.Start"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("GoLive.WebLiveActive")));
	CHECK(host.startCount == 0);
	CHECK(FindButton(dialog, Text("GoLive.Start"))->isEnabled());
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: an expired login on Start or Save shows nothing here; the menu tells")
{
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	controller.SetApi(&api);
	int lost = 0;
	controller.SetLoginLostCallback([&](ApiError::Kind) { ++lost; });
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);

	api.streamingError = {ApiError::Kind::SessionExpired, "99303", "expired"};
	const auto noModal = [](QDialog &) {
	};
	SUBCASE("Start Live")
	{
		CHECK_FALSE(WithModal([&]() { FindButton(dialog, Text("GoLive.Start"))->click(); }, noModal));
	}
	SUBCASE("Save")
	{
		CHECK_FALSE(WithModal([&]() { FindButton(dialog, Text("GoLive.Save"))->click(); }, noModal));
	}
	CHECK_FALSE(controller.IsLoggedIn());
	CHECK(lost == 1); // the menu's one notice
	CHECK(host.startCount == 0);
	controller.SetLoginLostCallback(nullptr);
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: an unusable backend answer says going live failed, not the network")
{
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	controller.SetApi(&api);
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);

	api.streamingError = {ApiError::Kind::InvalidResponse, "0", "no stream target"};
	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.Start"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("GoLive.StartFailed")));
	CHECK_FALSE(notice.contains(Text("Login.NetworkError")));
	CHECK(host.startCount == 0);
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: info the backend refuses shows the backend's reason")
{
	FakeLangLiveApi api;
	api.holdInfo = true;
	controller.SetApi(&api);
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();

	SUBCASE("a refusal with a reason")
	{
		api.streamingError = {ApiError::Kind::Other, "740", "bind a phone number first"};
		QString notice;
		CHECK(WithModal([&]() { api.AnswerHeldInfo(); }, [&](QDialog &modal) { notice = AllText(modal); }));
		CHECK(notice.contains(QStringLiteral("bind a phone number first")));
	}
	SUBCASE("no answer it understands")
	{
		api.streamingError = {ApiError::Kind::InvalidResponse, "", "not JSON"};
		QString notice;
		CHECK(WithModal([&]() { api.AnswerHeldInfo(); }, [&](QDialog &modal) { notice = AllText(modal); }));
		CHECK(notice.contains(Text("GoLive.LoadFailed")));
	}
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: a website live says so instead of the pre-stream check")
{
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	controller.SetApi(&api);
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);
	host.state.videoCodec = "hevc"; // would fail the check
	api.info.live = true;
	api.info.platform = StreamingInfo::Platform::Web;

	QString notice;
	CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.Start"))->click(); },
			[&](QDialog &modal) { notice = AllText(modal); }));
	CHECK(notice.contains(Text("GoLive.WebLiveActive")));
	CHECK_FALSE(notice.contains(Text("Preflight.VideoCodec")));
	CHECK(host.startCount == 0);
	controller.SetApi(nullptr);
}

TEST_CASE_FIXTURE(GoLiveFixture, "go-live: wrong OBS settings open the check before the backend, which goes live")
{
	FakeLangLiveApi api;
	api.info.channels = {{"84", "Chat"}};
	controller.SetApi(&api);
	ui::GoLiveDialog dialog(controller, nullptr);
	dialog.show();
	FillRequiredFields(dialog);
	host.state.videoCodec = "hevc";
	api.calls.clear();

	QString check;
	CHECK(WithModal([&]() { FindButton(dialog, Text("GoLive.Start"))->click(); },
			[&](QDialog &modal) {
				check = AllText(modal);
				// Only asked whether a website live runs; nothing saved or opened.
				CHECK(api.calls == std::vector<std::string>{"info"});
				FindButton(modal, Text("Preflight.ApplyAndStart"))->click();
			}));
	CHECK(check.contains(Text("Preflight.VideoCodec")));
	CHECK(host.state.videoCodec == "h264");
	CHECK(api.calls == std::vector<std::string>{"info", "info", "setting", "start"});
	CHECK(host.startCount == 1);
	controller.SetApi(nullptr);
}
