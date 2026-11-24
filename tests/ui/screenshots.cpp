// Not a test: with LANGLIVE_SCREENSHOT_DIR set, saves every dialog and notice
// as a PNG there, in the locale LANGLIVE_TEST_LOCALE names, so line breaks
// can be reviewed per language without OBS. scripts/dev/ui-screenshots.sh
// runs it for every locale and builds an HTML page of the results.

#include "ui_test_support.hpp"

#include "ui/cover-picker.hpp"
#include "ui/diagnostics-dialog.hpp"
#include "ui/go-live-dialog.hpp"
#include "ui/login-dialog.hpp"
#include "ui/message-dialog.hpp"
#include "ui/preflight-dialog.hpp"
#include "ui/update-dialog.hpp"

#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QImage>
#include <QLayout>
#include <QPixmap>
#include <QLineEdit>
#include <QTextStream>

using namespace langlive::obsplugin;
using namespace langlive::obsplugin::uitest;
using ui::Text;

namespace {

class Shots {
public:
	explicit Shots(QString dir) : dir_(std::move(dir)) { QDir().mkpath(dir_); }
	~Shots()
	{
		QFile file(dir_ + QStringLiteral("/shots.tsv"));
		if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			QTextStream out(&file);
			for (const auto &[name, caption] : list_)
				out << name << '\t' << caption << '\n';
		}
	}

	void Save(QWidget &widget, const QString &name, const QString &caption)
	{
		ProcessEvents();
		// Lay out again with every widget's style sheet font applied, as OBS's
		// event loop has by the time a dialog is seen, and grow the window if
		// the content needs more room.
		for (QWidget *child : widget.findChildren<QWidget *>())
			child->ensurePolished();
		if (QLayout *layout = widget.layout()) {
			layout->invalidate();
			layout->activate();
			widget.resize(widget.size().expandedTo(widget.minimumSizeHint()));
		}
		ProcessEvents();
		// Laid out at scale 1, as on screen, and drawn at twice the
		// resolution for sharp images (QT_SCALE_FACTOR breaks offscreen
		// layouts).
		QPixmap image(widget.size() * 2);
		image.setDevicePixelRatio(2);
		image.fill(Qt::transparent);
		widget.render(&image);
		image.save(dir_ + QLatin1Char('/') + name + QStringLiteral(".png"));
		list_.emplace_back(name, caption);
	}

	// A dialog shown with exec(): grabbed once it is up.
	void SaveModal(const std::function<void()> &open, const QString &name, const QString &caption)
	{
		WithModal(open, [&](QDialog &dialog) { Save(dialog, name, caption); });
	}

private:
	QString dir_;
	std::vector<std::pair<QString, QString>> list_;
};

} // namespace

TEST_CASE_FIXTURE(ControllerFixture, "screenshots: every dialog in the test locale (LANGLIVE_SCREENSHOT_DIR)")
{
	const QString dir = qEnvironmentVariable("LANGLIVE_SCREENSHOT_DIR");
	if (dir.isEmpty())
		return;
	// OBS's own font, as its default theme sets it on macOS (Open Sans at
	// 1.2 x the font scale of 10, so 12pt), loaded from the obs-studio
	// sources; the script sets QT_FONT_DPI=72 so points are macOS's.
	const QString fonts = qEnvironmentVariable("LANGLIVE_OBS_FONTS_DIR");
	if (!fonts.isEmpty()) {
		for (const QString &file : QDir(fonts).entryList({QStringLiteral("*.ttf")}))
			QFontDatabase::addApplicationFont(fonts + QLatin1Char('/') + file);
		QApplication::setFont(QFont(QStringLiteral("Open Sans"), 12));
	}
	// OBS's theme as OBS applies it to the whole app (scripts/dev/obs-theme-qss.py),
	// so the plugin's dialogs inherit what they do inside OBS.
	const QString theme = qEnvironmentVariable("LANGLIVE_OBS_THEME_QSS");
	if (QFile file(theme); !theme.isEmpty() && file.open(QIODevice::ReadOnly))
		qApp->setStyleSheet(QString::fromUtf8(file.readAll()));
	Shots shots(dir);
	// The version the update dialogs and notices show (buildspec.json's).
	const QString version = qEnvironmentVariable("LANGLIVE_SCREENSHOT_VERSION", QStringLiteral("1.0.0"));
	controller.SetUpdateFeed(nullptr, version.toStdString());

	// Login
	{
		ui::LoginDialog dialog(controller, nullptr);
		dialog.show();
		shots.Save(dialog, QStringLiteral("login-phone"), QStringLiteral("登入：手機號碼"));
		QLineEdit *phone = dialog.findChildren<QLineEdit *>().value(0);
		if (phone) {
			phone->setText(QStringLiteral("0912"));
			shots.Save(dialog, QStringLiteral("login-phone-invalid"),
				   QStringLiteral("登入：手機號碼格式錯誤"));
			phone->clear();
		}
		if (QPushButton *qr = FindButton(dialog, Text("Login.TabQr"))) {
			qr->click();
			shots.Save(dialog, QStringLiteral("login-qr"), QStringLiteral("登入：QR Code"));
		}
	}
	shots.SaveModal(
		[]() {
			ui::AskConfirmation(nullptr, Text("Logout.Title"), Text("Logout.Question"),
					    Text("Logout.Confirm"));
		},
		QStringLiteral("logout"), QStringLiteral("登出確認"));

	LogIn();

	// Go live
	{
		ui::GoLiveDialog dialog(controller, nullptr);
		dialog.show();
		shots.Save(dialog, QStringLiteral("golive"), QStringLiteral("開播"));
	}
	{
		FakeLangLiveApi api;
		api.holdInfo = true;
		controller.SetApi(&api);
		ui::GoLiveDialog dialog(controller, nullptr);
		dialog.show();
		shots.Save(dialog, QStringLiteral("golive-loading"), QStringLiteral("開播：載入中"));
		api.AnswerHeldInfo();
		controller.SetApi(nullptr);
	}
	shots.SaveModal(
		[]() {
			ui::AskConfirmation(nullptr, Text("EndLive.Title"), Text("EndLive.Question"),
					    Text("EndLive.Confirm"));
		},
		QStringLiteral("endlive"), QStringLiteral("結束直播確認"));
	{
		QImage image(1200, 800, QImage::Format_RGB32);
		image.fill(QColor(70, 90, 120));
		ui::CoverCropDialog dialog(image, [](const QImage &, std::function<void(bool, QString)>) {}, nullptr);
		dialog.show();
		shots.Save(dialog, QStringLiteral("cover-crop"), QStringLiteral("裁切封面"));
	}
	{
		using core::Violation;
		ui::PreflightDialog dialog(controller,
					   {Violation::UnsupportedVideoCodec, Violation::UnsupportedAudioCodec,
					    Violation::KeyframeIntervalTooLong, Violation::LandscapeResolutionMismatch,
					    Violation::FpsTooHigh1080p, Violation::BitrateTooHigh1080p},
					   nullptr);
		dialog.show();
		shots.Save(dialog, QStringLiteral("preflight"), QStringLiteral("開播前檢查（多項問題）"));
	}
	{
		GoLiveRequest request;
		request.settings.title = "Tonight";
		request.channelId = "mock-channel-1";
		if (testing::StartLive(controller, request).started() && controller.OnStreamingStarted().empty()) {
			ui::GoLiveDialog dialog(controller, nullptr);
			dialog.show();
			shots.Save(dialog, QStringLiteral("golive-live"), QStringLiteral("開播：直播中"));
		}
	}

	// Diagnostics
	{
		ui::DiagnosticsDialog dialog(nullptr);
		dialog.show();
		shots.Save(dialog, QStringLiteral("diagnostics"), QStringLiteral("問題排查"));
	}
	{
		ui::DiagnosticsExportedDialog dialog(
			QStringLiteral("/Users/streamer/Desktop/langlive_diagnostics_20261119.zip"), nullptr);
		dialog.show();
		shots.Save(dialog, QStringLiteral("diagnostics-exported"), QStringLiteral("問題排查：匯出成功"));
	}

	// Updates
	for (const auto &[result, name, caption] :
	     {std::tuple{UpdateCheck::Result::Available, "update-available", "版本更新：一般更新"},
	      std::tuple{UpdateCheck::Result::Required, "update-required", "版本更新：強制更新"}}) {
		UpdateCheck check;
		check.result = result;
		check.latestVersion = "1.1.0";
		ui::UpdateDialog dialog(controller, check, nullptr);
		dialog.show();
		shots.Save(dialog, QString::fromUtf8(name), QString::fromUtf8(caption));
	}

	// Notices
	const auto notice = [&](const QString &message, const char *name, const char *caption,
				const QString &title = QString(), const QString &button = QString()) {
		shots.SaveModal([&]() { ui::ShowMessage(nullptr, message, title, button); }, QString::fromUtf8(name),
				QString::fromUtf8(caption));
	};
	notice(Text("Login.Success").arg(QStringLiteral("小浪")), "msg-login-success", "通知：登入成功");
	notice(Text("Login.SessionExpired"), "msg-session-expired", "通知：登入失效");
	notice(Text("Login.NotEligible"), "msg-not-eligible", "通知：無開播權限");
	notice(Text("Login.SmsDailyLimit"), "msg-sms-limit", "通知：簡訊每日上限");
	notice(Text("Login.TooFrequent"), "msg-too-frequent", "通知：操作過於頻繁");
	notice(Text("Login.NetworkError"), "msg-network", "通知：網路錯誤");
	notice(Text("Login.SendCodeFailed"), "msg-send-code-failed", "通知：驗證碼傳送失敗");
	notice(Text("Login.Failed"), "msg-login-failed", "通知：登入失敗");
	notice(Text("Login.UpdateRequired"), "msg-login-update-required", "通知：需更新才能登入");
	notice(Text("GoLive.MissingTitle") + QLatin1Char('\n') + Text("GoLive.MissingChannel"), "msg-missing-fields",
	       "通知：必填欄位");
	notice(Text("GoLive.Saved"), "msg-saved", "通知：設定已儲存");
	notice(Text("GoLive.LoadFailed"), "msg-load-failed", "通知：直播間資訊載入失敗");
	notice(Text("GoLive.SaveFailed"), "msg-save-failed", "通知：儲存失敗");
	notice(Text("GoLive.StartFailed"), "msg-start-failed", "通知：開播失敗");
	notice(Text("GoLive.WebLiveActive"), "msg-web-live", "通知：Web 開播頁直播中");
	notice(Text("GoLive.CoverLoadFailed"), "msg-cover-load-failed", "通知：圖片無法讀取");
	notice(Text("GoLive.CoverSaveFailed"), "msg-cover-save-failed", "通知：封面無法儲存");
	notice(Text("GoLive.CoverUploadFailed"), "msg-cover-upload-failed", "通知：封面上傳失敗");
	notice(Text("LiveEnded.Outside"), "msg-live-ended", "通知：直播已結束（插件外停止）", Text("LiveEnded.Title"),
	       Text("LiveEnded.OK"));
	notice(Text("Preflight.ApplyFailed"), "msg-preflight-apply-failed", "通知：無法套用設定");
	notice(Text("Update.UpToDate").arg(version), "msg-update-uptodate", "通知：已是最新版本");
	notice(Text("Update.Failed"), "msg-update-failed", "通知：無法檢查更新");
	notice(Text("Update.DownloadFailed"), "msg-update-download-failed", "通知：更新下載失敗");
	notice(Text("Update.ChecksumMismatch"), "msg-update-checksum", "通知：安裝檔驗證失敗");
	notice(Text("Update.Installing"), "msg-update-installing", "通知：即將開啟安裝程式");
	notice(Text("Update.LaunchFailed"), "msg-update-launch-failed", "通知：無法開啟安裝程式");
	notice(Text("Update.LiveBlocked"), "msg-update-live-blocked", "通知：直播中無法更新");
	notice(Text("Diag.Failed").arg(QStringLiteral("Permission denied")), "msg-diag-failed", "通知：匯出失敗");
}
