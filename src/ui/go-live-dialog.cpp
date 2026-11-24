#include "ui/go-live-dialog.hpp"

#include "app/plugin-controller.hpp"
#include "ui/cover-picker.hpp"
#include "ui/message-dialog.hpp"
#include "ui/preflight-dialog.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace langlive::obsplugin::ui {

namespace {

constexpr int kTitleMaxLength = 15;
constexpr int kPushNotificationMaxLength = 20;
constexpr int kCoverJpegQuality = 90;
constexpr int kEscapeGraceMs = 500;

constexpr const char *kStyleSheet = R"(
	QScrollArea#content { border: none; background: transparent; }
	QWidget#contentInner { background: transparent; }
	QLabel#liveNotice {
		background-color: rgba(109, 100, 195, 0.18);
		border: 1px solid {brand};
		border-radius: 6px;
		padding: 10px 14px;
		font-size: 13px;
	}
	QLabel#liveNotice[warning="true"] {
		background-color: rgba(240, 160, 60, 0.16);
		border-color: {warning};
		color: {warning};
	}
	/* Where Tab is (see the constructor); text fields and choices show it already. */
	QPushButton#secondaryButton:focus { border-color: {brand}; }
	QPushButton#primaryButton:focus { border: 2px solid {text}; padding: 0 26px; }
	QRadioButton#scenarioCard:focus { border: 2px solid {brand}; padding: 9px 11px; }
	QWidget#loadingNotice QLabel { color: {secondaryText}; font-size: 13px; }
)";

// A spinning arc; it turns only while shown.
class Spinner : public QWidget {
public:
	explicit Spinner(QWidget *parent) : QWidget(parent)
	{
		setFixedSize(18, 18);
		timer_.setInterval(16);
		connect(&timer_, &QTimer::timeout, this, [this]() {
			angle_ = (angle_ + 6) % 360; // one turn a second
			update();
		});
	}

protected:
	void showEvent(QShowEvent *) override { timer_.start(); }
	void hideEvent(QHideEvent *) override { timer_.stop(); }
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		const QRectF circle = QRectF(rect()).adjusted(2, 2, -2, -2);
		painter.setPen(QPen(QColor(255, 255, 255, 40), 2.5));
		painter.drawEllipse(circle);
		painter.setPen(QPen(QColor(kBrandHoverColor), 2.5, Qt::SolidLine, Qt::RoundCap));
		painter.drawArc(circle, -angle_ * 16, 100 * 16);
	}

private:
	QTimer timer_;
	int angle_ = 0;
};

QComboBox *MakeChoiceCombo(const QString &placeholder, QWidget *parent)
{
	auto *combo = new QComboBox(parent);
	combo->addItem(placeholder, QString());
	return combo;
}

// Replaces the options after the placeholder, selecting `selectId` if it is
// one of them.
void SetChoices(QComboBox *combo, const std::vector<ChoiceOption> &options, const std::string &selectId)
{
	while (combo->count() > 1)
		combo->removeItem(1);
	for (const ChoiceOption &option : options)
		combo->addItem(QString::fromStdString(option.name), QString::fromStdString(option.id));
	const int index = combo->findData(QString::fromStdString(selectId));
	combo->setCurrentIndex(index > 0 ? index : 0);
}

// Label above a field, stacked.
QVBoxLayout *Field(QWidget *label, QWidget *field)
{
	auto *layout = new QVBoxLayout();
	layout->setSpacing(6);
	layout->addWidget(label);
	layout->addWidget(field);
	return layout;
}

std::string SelectedId(const QComboBox *combo)
{
	return combo->currentData().toString().toStdString();
}

} // namespace

void ShowStartLiveFailure(QWidget *parent, const StartLiveResult &result)
{
	// OBS has shown its own error.
	if (result.obsStartFailed)
		return;

	// Every missing required field is listed in one message.
	if (!result.missingFields.empty()) {
		QStringList messages;
		for (GoLiveFieldError error : result.missingFields) {
			switch (error) {
			case GoLiveFieldError::MissingTitle:
				messages << Text("GoLive.MissingTitle");
				break;
			case GoLiveFieldError::MissingChannel:
				messages << Text("GoLive.MissingChannel");
				break;
			}
		}
		ShowMessage(parent, messages.join(QLatin1Char('\n')));
		return;
	}

	switch (result.error.kind) {
	case ApiError::Kind::None:
	case ApiError::Kind::SessionExpired:
	case ApiError::Kind::NotEligible:
		return; // started, or logged out and the menu tells the streamer
	case ApiError::Kind::WebLiveActive:
		ShowMessage(parent, Text("GoLive.WebLiveActive"));
		return;
	case ApiError::Kind::Network:
		ShowMessage(parent, Text("Login.NetworkError"));
		return;
	case ApiError::Kind::InvalidResponse:
		// The backend answered, but not with what going live needs (e.g. no
		// stream target): not the streamer's network.
		ShowMessage(parent, Text("GoLive.StartFailed"));
		return;
	default:
		ShowMessage(parent, result.error.message.empty() ? Text("GoLive.StartFailed")
								 : QString::fromStdString(result.error.message));
		return;
	}
}

GoLiveDialog::GoLiveDialog(PluginController &controller, QWidget *parent) : QDialog(parent), controller_(controller)
{
	setWindowTitle(Text("GoLive.Title"));
	setMinimumWidth(600);
	ApplyDialogStyle(this, kStyleSheet);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	// The form can be taller than small screens, so it scrolls; the action
	// buttons stay visible below it.
	auto *scroll = new QScrollArea(this);
	scroll->setObjectName(QStringLiteral("content"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	// Not a stop for Tab: it scrolls to whichever field has the focus.
	scroll->setFocusPolicy(Qt::NoFocus);
	auto *inner = new QWidget(scroll);
	inner->setObjectName(QStringLiteral("contentInner"));
	auto *content = new QVBoxLayout(inner);
	content->setContentsMargins(28, 24, 28, 20);
	content->setSpacing(28);
	liveNotice_ = new QLabel(Text("GoLive.LiveNotice"), inner);
	liveNotice_->setObjectName(QStringLiteral("liveNotice"));
	liveNotice_->setWordWrap(true);
	content->addWidget(liveNotice_);
	loadingNotice_ = new QWidget(inner);
	loadingNotice_->setObjectName(QStringLiteral("loadingNotice"));
	auto *loadingRow = new QHBoxLayout(loadingNotice_);
	loadingRow->setContentsMargins(0, 0, 0, 0);
	loadingRow->setSpacing(10);
	loadingRow->addStretch();
	loadingRow->addWidget(new Spinner(loadingNotice_));
	loadingRow->addWidget(new QLabel(Text("GoLive.Loading"), loadingNotice_));
	loadingRow->addStretch();
	loadingNotice_->hide();
	content->addWidget(loadingNotice_);
	form_ = new QWidget(inner);
	auto *form = new QVBoxLayout(form_);
	form->setContentsMargins(0, 0, 0, 0);
	form->setSpacing(28);
	form->addWidget(CreateRoomSection());
	form->addWidget(CreateStreamSection());
	content->addWidget(form_);
	content->addStretch();
	scroll->setWidget(inner);
	outer->addWidget(scroll, 1);

	outer->addWidget(MakeDivider(this));
	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(28, 14, 28, 14);
	footer->setSpacing(10);
	footer->addStretch();
	saveButton_ = new QPushButton(Text("GoLive.Save"), this);
	saveButton_->setObjectName(QStringLiteral("secondaryButton"));
	startEndButton_ = new QPushButton(this);
	startEndButton_->setObjectName(QStringLiteral("primaryButton"));
	// Not the default button: Enter, or a key that dismissed another
	// dialog, must never start or end a live.
	for (QPushButton *button : {saveButton_, startEndButton_})
		button->setAutoDefault(false);
	for (QPushButton *button : {saveButton_, startEndButton_}) {
		button->setCursor(Qt::PointingHandCursor);
		footer->addWidget(button);
	}
	outer->addLayout(footer);

	connect(saveButton_, &QPushButton::clicked, this, &GoLiveDialog::OnSaveClicked);
	connect(startEndButton_, &QPushButton::clicked, this, &GoLiveDialog::OnStartEndClicked);

	// Tab follows the form from top to bottom. By default macOS moves Tab
	// only between text fields, which would skip all but the title and the
	// push text: StrongFocus keeps the buttons, choices and scenario cards
	// in the chain on every system. The cover itself is reached through
	// Upload Image next to it.
	const QList<QWidget *> tabOrder = {uploadButton_,  titleEdit_,     pushEdit_,     mainEventCombo_,
					   subEventCombo_, channelCombo_,  generalRadio_, highQualityRadio_,
					   saveButton_,    startEndButton_};
	for (QWidget *widget : tabOrder)
		widget->setFocusPolicy(Qt::StrongFocus);
	for (qsizetype i = 1; i < tabOrder.size(); ++i)
		setTabOrder(tabOrder[i - 1], tabOrder[i]);
	titleEdit_->setFocus();

	LoadSavedSettings();
	Refresh();
	ReloadInfo();

	// Fit the whole form without scrolling when the screen allows it.
	const int footerHeight = startEndButton_->sizeHint().height() + footer->contentsMargins().top() +
				 footer->contentsMargins().bottom() + 1;
	int maxHeight = 900;
	if (const QScreen *screen = parent ? parent->screen() : nullptr)
		maxHeight = std::min(maxHeight, screen->availableGeometry().height() * 9 / 10);
	resize(600, std::min(inner->sizeHint().height() + footerHeight, maxHeight));
}

QWidget *GoLiveDialog::CreateRoomSection()
{
	auto *section = new QWidget(this);
	auto *layout = new QVBoxLayout(section);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(16);

	auto *header = new QVBoxLayout();
	header->setSpacing(8);
	header->addWidget(MakeSectionTitle(Text("GoLive.RoomSection"), section));
	header->addWidget(MakeDivider(section));
	layout->addLayout(header);

	// Cover
	auto *coverBlock = new QVBoxLayout();
	coverBlock->setSpacing(6);
	coverBlock->addWidget(MakeFieldLabel(Text("GoLive.Cover"), section));
	auto *coverRow = new QHBoxLayout();
	coverRow->setSpacing(12);
	coverPicker_ = new CoverPicker(section);
	uploadButton_ = new QPushButton(Text("GoLive.UploadCover"), section);
	uploadButton_->setObjectName(QStringLiteral("secondaryButton"));
	uploadButton_->setCursor(Qt::PointingHandCursor);
	coverRow->addWidget(coverPicker_);
	coverRow->addWidget(uploadButton_, 0, Qt::AlignBottom);
	coverRow->addStretch();
	coverBlock->addLayout(coverRow);
	coverBlock->addWidget(MakeHintLabel(Text("GoLive.CoverHint"), section));
	layout->addLayout(coverBlock);

	// Title and push notification
	titleEdit_ = new QLineEdit(section);
	titleEdit_->setMaxLength(kTitleMaxLength);
	titleEdit_->setPlaceholderText(Text("GoLive.StreamTitlePlaceholder"));
	layout->addLayout(Field(MakeFieldLabel(Text("GoLive.StreamTitle"), section, true), titleEdit_));

	pushEdit_ = new QLineEdit(section);
	pushEdit_->setMaxLength(kPushNotificationMaxLength);
	pushEdit_->setPlaceholderText(Text("GoLive.PushNotificationPlaceholder"));
	layout->addLayout(Field(MakeFieldLabel(Text("GoLive.PushNotification"), section), pushEdit_));

	// Events and channel, filled by ReloadInfo
	mainEventCombo_ = MakeChoiceCombo(Text("GoLive.EventPlaceholder"), section);
	subEventCombo_ = MakeChoiceCombo(Text("GoLive.EventPlaceholder"), section);
	auto *events = new QGridLayout();
	events->setHorizontalSpacing(12);
	events->setVerticalSpacing(6);
	events->addWidget(MakeFieldLabel(Text("GoLive.MainEvent"), section), 0, 0);
	events->addWidget(MakeFieldLabel(Text("GoLive.SubEvent"), section), 0, 1);
	events->addWidget(mainEventCombo_, 1, 0);
	events->addWidget(subEventCombo_, 1, 1);
	events->setColumnStretch(0, 1);
	events->setColumnStretch(1, 1);
	layout->addLayout(events);

	channelCombo_ = MakeChoiceCombo(Text("GoLive.ChannelPlaceholder"), section);
	layout->addLayout(Field(MakeFieldLabel(Text("GoLive.Channel"), section, true), channelCombo_));

	connect(uploadButton_, &QPushButton::clicked, this, &GoLiveDialog::OnUploadCoverClicked);
	return section;
}

QWidget *GoLiveDialog::CreateStreamSection()
{
	auto *section = new QWidget(this);
	auto *layout = new QVBoxLayout(section);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(16);

	auto *header = new QVBoxLayout();
	header->setSpacing(8);
	header->addWidget(MakeSectionTitle(Text("GoLive.StreamSection"), section));
	header->addWidget(MakeDivider(section));
	layout->addLayout(header);

	orientationHint_ = new QLabel(section);
	orientationHint_->setWordWrap(true);
	layout->addWidget(orientationHint_);

	generalRadio_ = new QRadioButton(Text("Scenario.General"), section);
	highQualityRadio_ = new QRadioButton(Text("Scenario.HighQuality"), section);
	auto *cards = new QHBoxLayout();
	cards->setSpacing(12);
	auto *group = new QButtonGroup(section);
	for (QRadioButton *radio : {generalRadio_, highQualityRadio_}) {
		radio->setObjectName(QStringLiteral("scenarioCard"));
		radio->setCursor(Qt::PointingHandCursor);
		radio->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		group->addButton(radio);
		cards->addWidget(radio, 1);
	}

	auto *scenarioBlock = new QVBoxLayout();
	scenarioBlock->setSpacing(6);
	scenarioBlock->addWidget(MakeFieldLabel(Text("Scenario.Title"), section));
	scenarioBlock->addLayout(cards);
	layout->addLayout(scenarioBlock);
	return section;
}

void GoLiveDialog::LoadSavedSettings()
{
	const GoLiveSettings settings = controller_.Settings();
	titleEdit_->setText(QString::fromStdString(settings.title));
	pushEdit_->setText(QString::fromStdString(settings.pushNotification));
	(settings.scenario == core::Scenario::HighQuality ? highQualityRadio_ : generalRadio_)->setChecked(true);
}

void GoLiveDialog::Refresh()
{
	const bool liveFromPlugin = controller_.CurrentLiveSource() == LiveSource::Plugin;

	if (liveFromPlugin && std::exchange(closeWhenLive_, false)) {
		accept();
		return;
	}

	UpdateButtons();
	// The stream dropped and OBS is retrying: say so, the live is not over.
	const bool reconnecting = liveFromPlugin && controller_.IsReconnecting();
	liveNotice_->setVisible(liveFromPlugin);
	liveNotice_->setText(Text(reconnecting ? "GoLive.ReconnectingNotice" : "GoLive.LiveNotice"));
	if (liveNotice_->property("warning").toBool() != reconnecting) {
		liveNotice_->setProperty("warning", reconnecting);
		liveNotice_->style()->unpolish(liveNotice_);
		liveNotice_->style()->polish(liveNotice_);
	}
	setWindowTitle(Text(reconnecting     ? "GoLive.TitleReconnecting"
			    : liveFromPlugin ? "GoLive.TitleLive"
					     : "GoLive.Title"));

	const bool portrait = controller_.AccountOrientation() == core::Orientation::Portrait;
	orientationHint_->setText(
		Text("GoLive.OrientationHint").arg(Text(portrait ? "GoLive.Portrait" : "GoLive.Landscape")));
}

void GoLiveDialog::UpdateButtons()
{
	const bool liveFromPlugin = controller_.CurrentLiveSource() == LiveSource::Plugin;
	const bool starting = controller_.IsStartingLive();
	// Until streaming/info answers, Save does not know the backend's channel
	// (streaming/setting requires one) and Start has no channels to check.
	saveButton_->setEnabled(!liveFromPlugin && !saving_ && !starting && !loadingInfo_);
	loadingNotice_->setVisible(loadingInfo_ && !liveFromPlugin);
	// The form is locked while live (only End Live is available) and while
	// streaming/info loads, which fills it.
	form_->setEnabled(!liveFromPlugin && !loadingInfo_);
	saveButton_->setText(Text(saving_ ? "GoLive.Saving" : "GoLive.Save"));
	startEndButton_->setEnabled(!saving_ && !starting && (liveFromPlugin || !loadingInfo_));
	startEndButton_->setText(Text(starting ? "GoLive.Starting" : liveFromPlugin ? "GoLive.End" : "GoLive.Start"));
}

void GoLiveDialog::ReloadInfo()
{
	if (loadingInfo_)
		return;
	loadingInfo_ = true;
	UpdateButtons();

	QPointer<GoLiveDialog> self = this;
	controller_.LoadGoLiveInfo([self](ApiError error, GoLiveInfo info) {
		if (!self)
			return;
		self->loadingInfo_ = false;
		self->UpdateButtons();
		// The locked form lost the focus; give it back to the title.
		if (self->form_->isEnabled() && !self->form_->isAncestorOf(self->focusWidget()))
			self->titleEdit_->setFocus();
		if (error.endsLogin())
			return; // logged out; the menu tells the streamer
		if (!error.ok()) {
			// The backend's own reason when it refused (e.g. 740, the account
			// has no phone number bound), as Start Live does.
			if (error.kind == ApiError::Kind::Network)
				ShowMessage(self, Text("Login.NetworkError"));
			else if (error.kind == ApiError::Kind::Other && !error.message.empty())
				ShowMessage(self, QString::fromStdString(error.message));
			else
				ShowMessage(self, Text("GoLive.LoadFailed"));
			return;
		}
		self->ApplyInfo(info);
		self->Refresh(); // the orientation may have changed
	});
}

void GoLiveDialog::ApplyInfo(const GoLiveInfo &info)
{
	const bool first = !std::exchange(infoLoaded_, true);
	// The backend keeps title and push notification (Save); fill them once,
	// so a reload never overwrites what the streamer typed.
	if (first) {
		if (!info.title.empty())
			titleEdit_->setText(QString::fromStdString(info.title));
		if (!info.pushNotification.empty())
			pushEdit_->setText(QString::fromStdString(info.pushNotification));
	}
	// The channel and events are not saved (spec 4.3): keep only what was
	// chosen in this session.
	SetChoices(channelCombo_, info.channels, SelectedId(channelCombo_));
	SetChoices(mainEventCombo_, info.events, SelectedId(mainEventCombo_));
	SetChoices(subEventCombo_, info.events, SelectedId(subEventCombo_));
	ShowCover(info.coverUrl);
}

void GoLiveDialog::ShowCover(const std::string &url)
{
	// Downloaded every time the info loads: the website can change the
	// room's cover too.
	shownCoverUrl_ = url;
	if (url.empty()) {
		coverPicker_->SetImage({});
		return;
	}
	QPointer<GoLiveDialog> self = this;
	controller_.LoadCoverImage(url, [self, url](std::string bytes) {
		if (!self || self->shownCoverUrl_ != url)
			return; // a newer cover was shown meanwhile
		const QImage image =
			QImage::fromData(reinterpret_cast<const uchar *>(bytes.data()), static_cast<int>(bytes.size()));
		// Not downloadable: "No cover uploaded", though the live still
		// starts with it.
		self->coverPicker_->SetImage(image);
	});
}

void GoLiveDialog::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape && ignoreEscape_) {
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event); // Escape hides the dialog; the form is kept
}

void GoLiveDialog::CancelPendingClose()
{
	closeWhenLive_ = false;
}

void GoLiveDialog::OnUploadCoverClicked()
{
	QStringList patterns;
	for (const QByteArray &format : QImageReader::supportedImageFormats())
		patterns << QStringLiteral("*.%1").arg(QString::fromLatin1(format));

	ignoreEscape_ = true;
	const QString path = QFileDialog::getOpenFileName(
		this, Text("GoLive.UploadCover"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
		QStringLiteral("%1 (%2)").arg(Text("GoLive.ImageFiles"), patterns.join(QLatin1Char(' '))));
	QTimer::singleShot(kEscapeGraceMs, this, [this]() { ignoreEscape_ = false; });
	// When the native picker closes, macOS brings OBS's main window to the
	// front and this modeless dialog ends up behind it. Bring it back once
	// AppKit has finished reordering.
	QTimer::singleShot(0, this, [this]() {
		raise();
		activateWindow();
	});
	if (path.isEmpty())
		return;

	QImageReader reader(path);
	reader.setAutoTransform(true); // honor EXIF rotation from phone photos
	const QImage image = reader.read();
	if (image.isNull()) {
		ShowMessage(this, Text("GoLive.CoverLoadFailed"));
		return;
	}

	CoverCropDialog crop(
		image,
		[this](const QImage &cover, std::function<void(bool, QString)> done) { UploadCover(cover, done); },
		this);
	crop.exec();
}

void GoLiveDialog::UploadCover(const QImage &cover, std::function<void(bool, QString)> done)
{
	// A file only for the upload; the cover shown afterwards is the image.
	const QString uploadPath = QString::fromStdString(controller_.CoverImagePath());
	if (uploadPath.isEmpty() || !cover.save(uploadPath, "JPG", kCoverJpegQuality)) {
		QFile::remove(uploadPath);
		done(false, Text("GoLive.CoverSaveFailed"));
		return;
	}

	QPointer<GoLiveDialog> self = this;
	controller_.UploadCover(uploadPath.toStdString(), [self, cover, uploadPath,
							   done = std::move(done)](ApiError error, std::string url) {
		QFile::remove(uploadPath);
		if (!error.ok()) {
			if (error.endsLogin())
				done(false, {}); // logged out; the menu tells the streamer
			else if (error.kind == ApiError::Kind::Network)
				done(false, Text("Login.NetworkError"));
			else if (error.kind == ApiError::Kind::Other && !error.message.empty())
				done(false, QString::fromStdString(error.message)); // e.g. the daily limit
			else
				done(false, Text("GoLive.CoverUploadFailed"));
			return;
		}
		if (self) {
			self->controller_.UseUploadedCover(url);
			self->shownCoverUrl_ = url;
			self->coverPicker_->SetImage(cover);
		}
		done(true, {});
	});
}

GoLiveRequest GoLiveDialog::CollectRequest()
{
	GoLiveRequest request;
	request.settings.title = titleEdit_->text().trimmed().toStdString();
	request.settings.pushNotification = pushEdit_->text().trimmed().toStdString();
	request.settings.scenario = highQualityRadio_->isChecked() ? core::Scenario::HighQuality
								   : core::Scenario::General;
	request.channelId = SelectedId(channelCombo_);
	request.mainEventId = SelectedId(mainEventCombo_);
	request.subEventId = SelectedId(subEventCombo_);
	return request;
}

void GoLiveDialog::OnSaveClicked()
{
	if (saving_)
		return;
	saving_ = true;
	UpdateButtons();

	// Saving also applies the scenario's stream settings to OBS.
	QPointer<GoLiveDialog> self = this;
	controller_.SaveSettings(CollectRequest(), [self](SaveSettingsResult result) {
		if (!self)
			return;
		self->saving_ = false;
		self->UpdateButtons();
		if (result.error.endsLogin())
			return; // logged out; the menu tells the streamer
		if (!result.applied) {
			ShowMessage(self, Text("Preflight.ApplyFailed"));
			return;
		}
		if (!result.error.ok()) {
			// The backend's own message when it refused (a sensitive word,
			// the daily upload limit), the generic one otherwise.
			ShowMessage(self, result.error.kind == ApiError::Kind::Other && !result.error.message.empty()
						  ? QString::fromStdString(result.error.message)
						  : Text("GoLive.SaveFailed"));
			return;
		}
		ShowMessage(self, Text("GoLive.Saved"));
	});
}

void GoLiveDialog::OnStartEndClicked()
{
	// A dialog is still open over this one (e.g. the pre-stream check):
	// finish that first.
	if (QApplication::activeModalWidget() || controller_.IsStartingLive())
		return;

	if (controller_.CurrentLiveSource() == LiveSource::Plugin) {
		const bool confirmed =
			AskConfirmation(this, Text("EndLive.Title"), Text("EndLive.Question"), Text("EndLive.Confirm"));
		// The stream may have stopped on its own while the question was open.
		if (confirmed && controller_.CurrentLiveSource() == LiveSource::Plugin)
			controller_.EndLive();
		return;
	}

	QPointer<GoLiveDialog> self = this;
	controller_.StartLive(CollectRequest(), [self](StartLiveResult result) {
		if (!self)
			return;
		self->UpdateButtons();
		if (result.started()) {
			// Close once the stream passes the pre-stream check and the live
			// starts (see Refresh). If the check fails, the form stays open
			// under the check's dialog.
			self->closeWhenLive_ = true;
			return;
		}

		if (!result.violations.empty()) {
			// Nothing was sent to the backend yet. The check's dialog applies
			// the settings and starts the live again.
			PreflightDialog preflight(self->controller_, std::move(result.violations), self);
			const bool restarted = preflight.exec() == QDialog::Accepted;
			if (!self)
				return;
			if (restarted)
				self->closeWhenLive_ = true;
			// macOS brings OBS's main window to the front over this dialog.
			QTimer::singleShot(0, self, [self]() {
				self->raise();
				self->activateWindow();
			});
			return;
		}
		ShowStartLiveFailure(self, result);
	});
}

} // namespace langlive::obsplugin::ui
