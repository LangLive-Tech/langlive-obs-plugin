#include "ui/login-dialog.hpp"

#include "app/plugin-controller.hpp"
#include "langlive/obsplugin/core/phone.hpp"
#include "ui/qr-code-widget.hpp"
#include "ui/message-dialog.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"

#include <obs-module.h>

#include <QButtonGroup>
#include <QComboBox>
#include <QEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QValidator>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace langlive::obsplugin::ui {

namespace {

constexpr int kPhonePage = 0;
constexpr int kQrPage = 1;
constexpr int kLogoSize = 40;

constexpr int kSmsCodeLength = 6;
// The countries the LangLive backend supports for phone login, Taiwan first
// as the default. Shown with a region prefix like the website ("TW+886",
// including its "MC" for Macau); the code alone goes to the backend.
struct CountryCode {
	const char *region;
	const char *code;
};
constexpr CountryCode kCountryCodes[] = {{"TW", "+886"}, {"CN", "+86"}, {"HK", "+852"}, {"MC", "+853"}, {"SG", "+65"},
					 {"MY", "+60"},  {"JP", "+81"}, {"KR", "+82"},  {"TH", "+66"},  {"KH", "+855"}};

// Same layout as the website's login dialog: title area, tab bar, body.
constexpr const char *kStyleSheet = R"(
	QFrame#loginHeader { background-color: {header}; border: none; }
	QLabel#loginHeading { font-size: 22px; font-weight: 600; }
	QFrame#loginTabBar { background-color: {tabBar}; border: none; border-bottom: 1px solid {border}; }
	QPushButton#loginTab {
		background: transparent;
		color: {secondaryText};
		border: none;
		border-bottom: 2px solid transparent;
		border-radius: 0;
		min-height: 46px;
		font-size: 16px;
	}
	QPushButton#loginTab:hover { color: {text}; }
	QPushButton#loginTab:checked { color: {brand}; border-bottom: 2px solid {brand}; }
	QLabel#qrExpiredLabel { color: #1f1f1f; font-size: 15px; font-weight: 600; }
	QLineEdit[required="true"] { border-color: {error}; }
)";

// Reports a line edit losing focus while empty. Not editingFinished: since
// Qt 6 it fires on focus loss only if the text changed, so clicking into an
// empty field and out again would say nothing.
class LeftEmptyFilter final : public QObject {
public:
	LeftEmptyFilter(QLineEdit *edit, std::function<void()> leftEmpty)
		: QObject(edit),
		  leftEmpty_(std::move(leftEmpty))
	{
		edit->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		// Not when another window takes the focus (switching apps, a popup):
		// the streamer has not moved on from the field.
		if (event->type() == QEvent::FocusOut && static_cast<QLineEdit *>(watched)->text().isEmpty()) {
			const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
			if (reason != Qt::ActiveWindowFocusReason && reason != Qt::PopupFocusReason)
				leftEmpty_();
		}
		return false;
	}

private:
	std::function<void()> leftEmpty_;
};

// Up to `maxDigits` digits. Full-width digits (０-９, from an input method)
// become ASCII ones instead of being refused.
class DigitsValidator final : public QValidator {
public:
	DigitsValidator(int maxDigits, QObject *parent) : QValidator(parent), maxDigits_(maxDigits) {}

	State validate(QString &input, int &) const override
	{
		for (QChar &c : input)
			if (c.unicode() >= 0xFF10 && c.unicode() <= 0xFF19)
				c = QChar(u'0' + (c.unicode() - 0xFF10));
		if (input.size() > maxDigits_)
			return Invalid;
		for (const QChar c : input)
			if (c < u'0' || c > u'9')
				return Invalid;
		return Acceptable;
	}

private:
	int maxDigits_;
};

// A field for a phone number or an SMS code. Its input method is off, like a
// password field's: with a Chinese input method active (Zhuyin takes 3, 4,
// 6, 7 as tones), the number keys still type digits, without switching.
void MakeDigitsOnly(QLineEdit *edit, int maxDigits)
{
	edit->setValidator(new DigitsValidator(maxDigits, edit));
	edit->setInputMethodHints(Qt::ImhDigitsOnly);
	edit->setAttribute(Qt::WA_InputMethodEnabled, false);
}

// "Required" under a field; it keeps its space when hidden, so showing it
// never moves the rest of the form.
QLabel *MakeRequiredLabel(QWidget *parent)
{
	auto *label = new QLabel(Text("Login.Required"), parent);
	label->setObjectName(QStringLiteral("errorLabel"));
	QSizePolicy policy = label->sizePolicy();
	policy.setRetainSizeWhenHidden(true);
	label->setSizePolicy(policy);
	label->hide();
	return label;
}

QLabel *MakeLogo(QWidget *parent)
{
	auto *label = new QLabel(parent);
	label->setAlignment(Qt::AlignCenter);

	QPixmap logo;
	if (char *path = obs_module_file("images/logo.png")) {
		logo.load(QString::fromUtf8(path));
		bfree(path);
	}
	if (!logo.isNull()) {
		const qreal ratio = label->devicePixelRatioF();
		QPixmap scaled =
			logo.scaled(QSize(kLogoSize, kLogoSize) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		scaled.setDevicePixelRatio(ratio);
		label->setPixmap(scaled);
	}
	return label;
}

QString Link(const std::string &url, const QString &text)
{
	return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
		.arg(QString::fromStdString(url).toHtmlEscaped(), QString::fromUtf8(kBrandColor), text.toHtmlEscaped());
}

QString FormatCountdown(int seconds)
{
	return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

LoginDialog::LoginDialog(PluginController &controller, QWidget *parent) : QDialog(parent), controller_(controller)
{
	setWindowTitle(Text("Login.Title"));
	setFixedWidth(400);
	ApplyDialogStyle(this, kStyleSheet);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	auto *header = new QFrame(this);
	header->setObjectName(QStringLiteral("loginHeader"));
	auto *headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(24, 22, 24, 22);
	headerLayout->setSpacing(10);
	headerLayout->addStretch();
	headerLayout->addWidget(MakeLogo(header));
	auto *heading = new QLabel(Text("Login.Heading"), header);
	heading->setObjectName(QStringLiteral("loginHeading"));
	headerLayout->addWidget(heading);
	headerLayout->addStretch();
	outer->addWidget(header);

	auto *tabBar = new QFrame(this);
	tabBar->setObjectName(QStringLiteral("loginTabBar"));
	auto *tabs = new QHBoxLayout(tabBar);
	tabs->setContentsMargins(0, 0, 0, 0);
	tabs->setSpacing(0);
	phoneTab_ = new QPushButton(Text("Login.TabPhone"), tabBar);
	qrTab_ = new QPushButton(Text("Login.TabQr"), tabBar);
	auto *tabGroup = new QButtonGroup(this);
	for (QPushButton *tab : {phoneTab_, qrTab_}) {
		tab->setObjectName(QStringLiteral("loginTab"));
		tab->setCheckable(true);
		tab->setCursor(Qt::PointingHandCursor);
		tab->setFocusPolicy(Qt::TabFocus);
		tabGroup->addButton(tab);
		tabs->addWidget(tab, 1);
	}
	outer->addWidget(tabBar);

	auto *layout = new QVBoxLayout();
	layout->setContentsMargins(32, 24, 32, 20);
	layout->setSpacing(0);
	outer->addLayout(layout);

	pages_ = new QStackedWidget(this);
	pages_->addWidget(CreatePhonePage());
	pages_->addWidget(CreateQrPage());
	layout->addWidget(pages_);
	layout->addSpacing(16);

	auto *consent = MakeHintLabel(
		Text("Login.Consent")
			.arg(Link(controller_.WebsitePageUrl(core::WebsitePage::Terms), Text("Login.Terms")),
			     Link(controller_.WebsitePageUrl(core::WebsitePage::Privacy), Text("Login.Privacy"))),
		this, Qt::AlignCenter);
	consent->setTextFormat(Qt::RichText);
	consent->setOpenExternalLinks(true);
	consent->setTextInteractionFlags(Qt::TextBrowserInteraction);
	layout->addWidget(consent);
	layout->addSpacing(8);

	layout->addWidget(MakeHintLabel(Text("Login.ScamNotice"), this, Qt::AlignCenter));

	connect(phoneTab_, &QPushButton::clicked, this, [this]() { ShowPage(kPhonePage); });
	connect(qrTab_, &QPushButton::clicked, this, [this]() { ShowPage(kQrPage); });

	ShowPage(kPhonePage);

	// One size for both tabs (the taller QR page), so switching tabs never
	// resizes or moves the dialog. The phone page's hint and error lines
	// fit in the difference.
	setFixedHeight(sizeHint().height());
}

QWidget *LoginDialog::CreatePhonePage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(6);

	layout->addWidget(MakeFieldLabel(Text("Login.PhoneNumber"), page));
	auto *phoneRow = new QHBoxLayout();
	phoneRow->setSpacing(8);
	countryCode_ = new QComboBox(page);
	for (const CountryCode &country : kCountryCodes)
		countryCode_->addItem(QString::fromUtf8(country.region) + QString::fromUtf8(country.code),
				      QString::fromUtf8(country.code));
	countryCode_->setFixedWidth(112);
	phoneEdit_ = new QLineEdit(page);
	phoneEdit_->setPlaceholderText(Text("Login.PhonePlaceholder"));
	MakeDigitsOnly(phoneEdit_, 15);
	phoneRow->addWidget(countryCode_);
	phoneRow->addWidget(phoneEdit_, 1);
	layout->addLayout(phoneRow);
	phoneRequired_ = MakeRequiredLabel(page);
	// Under the number field, not the country code: it is about the number.
	// (Style sheets reset a label's own margins, so a spacing does it.)
	auto *phoneHintRow = new QHBoxLayout();
	phoneHintRow->setSpacing(0);
	phoneHintRow->addSpacing(countryCode_->width() + phoneRow->spacing());
	phoneHintRow->addWidget(phoneRequired_, 1);
	layout->addLayout(phoneHintRow);

	layout->addWidget(MakeFieldLabel(Text("Login.SmsCode"), page));
	auto *codeRow = new QHBoxLayout();
	codeRow->setSpacing(8);
	codeEdit_ = new QLineEdit(page);
	codeEdit_->setPlaceholderText(Text("Login.SmsCodePlaceholder"));
	MakeDigitsOnly(codeEdit_, kSmsCodeLength);
	sendCodeButton_ = new QPushButton(Text("Login.SendCode"), page);
	sendCodeButton_->setObjectName(QStringLiteral("secondaryButton"));
	sendCodeButton_->setCursor(Qt::PointingHandCursor);
	sendCodeButton_->setMinimumWidth(120);
	MatchFieldHeight(sendCodeButton_);
	codeEdit_->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	// Both centered in the row: OBS's theme makes a button's size hint taller
	// than its height cap, and the layout would otherwise place it lower.
	codeRow->addWidget(codeEdit_, 1, Qt::AlignVCenter);
	codeRow->addWidget(sendCodeButton_, 0, Qt::AlignVCenter);
	layout->addLayout(codeRow);
	codeRequired_ = MakeRequiredLabel(page);
	layout->addWidget(codeRequired_);

	codeSentHint_ = new QLabel(page);
	codeSentHint_->setObjectName(QStringLiteral("hintLabel"));
	codeSentHint_->hide();
	layout->addWidget(codeSentHint_);

	phoneError_ = new QLabel(page);
	phoneError_->setObjectName(QStringLiteral("errorLabel"));
	phoneError_->hide();
	layout->addWidget(phoneError_);
	layout->addSpacing(14);

	submitButton_ = new QPushButton(Text("Login.Submit"), page);
	submitButton_->setObjectName(QStringLiteral("primaryButton"));
	submitButton_->setCursor(Qt::PointingHandCursor);
	submitButton_->setDefault(true);
	layout->addWidget(submitButton_);
	layout->addStretch();

	resendTimer_ = new QTimer(this);
	resendTimer_->setInterval(1000);

	connect(sendCodeButton_, &QPushButton::clicked, this, &LoginDialog::OnSendCodeClicked);
	connect(resendTimer_, &QTimer::timeout, this, &LoginDialog::OnResendTick);
	connect(submitButton_, &QPushButton::clicked, this, &LoginDialog::OnPhoneLoginClicked);
	connect(codeEdit_, &QLineEdit::returnPressed, this, &LoginDialog::OnPhoneLoginClicked);
	connect(phoneEdit_, &QLineEdit::textEdited, phoneError_, &QLabel::hide);
	connect(codeEdit_, &QLineEdit::textEdited, phoneError_, &QLabel::hide);
	connect(phoneEdit_, &QLineEdit::textChanged, this, &LoginDialog::UpdatePhoneButtons);
	connect(codeEdit_, &QLineEdit::textChanged, this, &LoginDialog::UpdatePhoneButtons);
	connect(countryCode_, &QComboBox::currentIndexChanged, this, &LoginDialog::UpdatePhoneButtons);
	WatchRequired(phoneEdit_, phoneRequired_);
	WatchRequired(codeEdit_, codeRequired_);
	// After WatchRequired, which hides the label once the field has text.
	connect(phoneEdit_, &QLineEdit::textChanged, this, &LoginDialog::UpdatePhoneHint);
	connect(countryCode_, &QComboBox::currentIndexChanged, this, &LoginDialog::UpdatePhoneHint);
	// Like the website: once the phone number is complete, the code is what
	// is still missing.
	const auto codeNext = [this]() {
		if (codeEdit_->text().isEmpty() &&
		    core::IsValidMobileNumber(countryCode_->currentData().toString().toStdString(),
					      phoneEdit_->text().toStdString()))
			MarkRequired(codeEdit_, codeRequired_, true);
	};
	connect(phoneEdit_, &QLineEdit::textChanged, this, codeNext);
	connect(countryCode_, &QComboBox::currentIndexChanged, this, codeNext);
	UpdatePhoneButtons();
	return page;
}

QWidget *LoginDialog::CreateQrPage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(10);

	qrCode_ = new QrCodeWidget(page);
	layout->addWidget(qrCode_, 0, Qt::AlignHCenter);

	auto *instruction = new QLabel(Text("Login.QrInstruction"), page);
	instruction->setAlignment(Qt::AlignCenter);
	instruction->setWordWrap(true);
	layout->addWidget(instruction);

	qrCountdown_ = MakeHintLabel(QString(), page, Qt::AlignCenter);
	layout->addWidget(qrCountdown_);
	layout->addStretch();

	qrTimer_ = new QTimer(this);
	qrTimer_->setInterval(1000);
	connect(qrTimer_, &QTimer::timeout, this, &LoginDialog::OnQrTick);
	connect(qrCode_, &QrCodeWidget::RefreshRequested, this, &LoginDialog::StartQrLogin);
	return page;
}

void LoginDialog::ShowPage(int index)
{
	pages_->setCurrentIndex(index);
	(index == kQrPage ? qrTab_ : phoneTab_)->setChecked(true);

	if (index == kQrPage) {
		if (!qrStarted_)
			StartQrLogin();
	} else {
		phoneEdit_->setFocus();
	}
}

void LoginDialog::SetWaiting(bool waiting)
{
	waiting_ = waiting;
	submitButton_->setText(Text(waiting ? "Login.Waiting" : "Login.Submit"));
	UpdatePhoneButtons();
}

void LoginDialog::UpdatePhoneButtons()
{
	const bool phoneValid = core::IsValidMobileNumber(countryCode_->currentData().toString().toStdString(),
							  phoneEdit_->text().toStdString());
	sendCodeButton_->setEnabled(!waiting_ && phoneValid && !resendTimer_->isActive());
	// Only after Send code: a code typed without one, or for another
	// number, cannot log in.
	submitButton_->setEnabled(!waiting_ && phoneValid && codeEdit_->text().size() == kSmsCodeLength &&
				  !codeSentTo_.empty() && codeSentTo_ == Cellphone());
}

void LoginDialog::UpdatePhoneHint()
{
	// An empty field is "Required", shown once the streamer leaves it.
	if (phoneEdit_->text().isEmpty()) {
		phoneRequired_->setText(Text("Login.Required"));
		return;
	}
	const bool valid = core::IsValidMobileNumber(countryCode_->currentData().toString().toStdString(),
						     phoneEdit_->text().toStdString());
	phoneRequired_->setText(Text("Login.InvalidPhone"));
	MarkRequired(phoneEdit_, phoneRequired_, !valid);
}

std::string LoginDialog::Cellphone() const
{
	return core::FormatCellphone(countryCode_->currentData().toString().toStdString(),
				     phoneEdit_->text().toStdString());
}

void LoginDialog::OnSendCodeClicked()
{
	if (!sendCodeButton_->isEnabled())
		return;
	SetWaiting(true);

	QPointer<LoginDialog> self = this;
	controller_.RequestSmsCode(
		countryCode_->currentData().toString().toStdString(), phoneEdit_->text().toStdString(),
		[self, cellphone = Cellphone()](SmsCodeRequest request) {
			if (!self)
				return;
			self->SetWaiting(false);
			switch (request.result) {
			case SmsCodeRequest::Result::Sent:
				break;
			case SmsCodeRequest::Result::InvalidPhoneNumber:
				self->ShowPhoneError(Text("Login.InvalidPhone"));
				self->phoneEdit_->setFocus();
				return;
			case SmsCodeRequest::Result::DailyLimitReached:
				self->ShowPhoneError(Text("Login.SmsDailyLimit"));
				return;
			case SmsCodeRequest::Result::TooFrequent:
				self->ShowPhoneError(Text("Login.TooFrequent"));
				return;
			case SmsCodeRequest::Result::UpdateRequired:
				self->ShowPhoneError(Text("Login.UpdateRequired"));
				return;
			case SmsCodeRequest::Result::NetworkError:
				self->ShowPhoneError(Text("Login.NetworkError"));
				return;
			case SmsCodeRequest::Result::Failed:
				self->ShowPhoneError(request.message.empty() ? Text("Login.SendCodeFailed")
									     : QString::fromStdString(request.message));
				return;
			}

			self->codeSentTo_ = cellphone;
			self->phoneError_->hide();
			self->codeSentHint_->setText(
				Text("Login.SmsCodeSent")
					.arg(QStringLiteral("%1 %2").arg(self->countryCode_->currentData().toString(),
									 self->phoneEdit_->text())));
			self->codeSentHint_->show();
			self->codeEdit_->setFocus();

			self->resendSecondsLeft_ = request.resendAfterSeconds;
			self->sendCodeButton_->setText(Text("Login.ResendIn").arg(self->resendSecondsLeft_));
			self->resendTimer_->start();
			self->UpdatePhoneButtons();
		});
}

void LoginDialog::OnResendTick()
{
	if (--resendSecondsLeft_ > 0) {
		sendCodeButton_->setText(Text("Login.ResendIn").arg(resendSecondsLeft_));
		return;
	}
	resendTimer_->stop();
	sendCodeButton_->setText(Text("Login.Resend"));
	UpdatePhoneButtons();
}

void LoginDialog::OnPhoneLoginClicked()
{
	// Also reached by Enter in the code field.
	if (!submitButton_->isEnabled())
		return;
	SetWaiting(true);

	QPointer<LoginDialog> self = this;
	controller_.LoginWithSmsCode(
		countryCode_->currentData().toString().toStdString(), phoneEdit_->text().toStdString(),
		codeEdit_->text().toStdString(), [self](SmsLoginResponse response) {
			if (!self)
				return;
			self->SetWaiting(false);
			switch (response.result) {
			case SmsLoginResponse::Result::Success:
				break;
			case SmsLoginResponse::Result::NotEligible:
				self->phoneError_->hide();
				ShowMessage(self, Text("Login.NotEligible"));
				return;
			case SmsLoginResponse::Result::UpdateRequired:
				self->ShowPhoneError(Text("Login.UpdateRequired"));
				return;
			case SmsLoginResponse::Result::NetworkError:
				self->ShowPhoneError(Text("Login.NetworkError"));
				return;
			case SmsLoginResponse::Result::Failed:
				// The backend's own reason, e.g. a wrong code.
				self->ShowPhoneError(response.message.empty()
							     ? Text("Login.Failed")
							     : QString::fromStdString(response.message));
				self->codeEdit_->setFocus();
				return;
			}

			ShowMessage(self,
				    Text("Login.Success").arg(QString::fromStdString(self->controller_.Nickname())));
			self->accept();
		});
}

void LoginDialog::MarkRequired(QLineEdit *edit, QLabel *required, bool missing)
{
	required->setVisible(missing);
	edit->setProperty("required", missing);
	// Dynamic properties need a re-polish to restyle.
	edit->style()->unpolish(edit);
	edit->style()->polish(edit);
}

void LoginDialog::WatchRequired(QLineEdit *edit, QLabel *required)
{
	const auto mark = [edit, required](bool missing) {
		MarkRequired(edit, required, missing);
	};
	// Leaving the field empty: by focus, or with Return.
	new LeftEmptyFilter(edit, [mark]() { mark(true); });
	connect(edit, &QLineEdit::returnPressed, required, [edit, mark]() {
		if (edit->text().isEmpty())
			mark(true);
	});
	connect(edit, &QLineEdit::textChanged, required, [mark](const QString &text) {
		if (!text.isEmpty())
			mark(false);
	});
}

void LoginDialog::ShowPhoneError(const QString &message)
{
	phoneError_->setText(message);
	phoneError_->show();
}

void LoginDialog::StartQrLogin()
{
	qrStarted_ = true;
	qrActive_ = false;
	qrScanned_ = false;
	qrTimer_->stop();
	qrCode_->SetContent({});
	qrCode_->SetExpired(false);
	qrCountdown_->setText(Text("Login.QrLoading"));
	qrCountdown_->show();

	QPointer<LoginDialog> self = this;
	controller_.StartQrLogin([self](QrLoginUpdate update) {
		if (!self)
			return;
		switch (update.result) {
		case QrLoginUpdate::Result::Waiting:
			break;
		case QrLoginUpdate::Result::UpdateRequired:
			self->EndQrLogin(Text("Login.UpdateRequired"));
			return;
		case QrLoginUpdate::Result::NetworkError:
			self->EndQrLogin(Text("Login.NetworkError"));
			return;
		default:
			self->EndQrLogin(update.message.empty() ? Text("Login.QrFailed")
								: QString::fromStdString(update.message));
			return;
		}
		self->qrActive_ = true;
		self->qrSecondsLeft_ = update.expiresInSeconds;
		self->qrCode_->SetContent(update.content);
		self->qrCountdown_->setText(Text("Login.QrExpiresIn").arg(FormatCountdown(self->qrSecondsLeft_)));
		self->qrTimer_->start();
	});
}

void LoginDialog::OnQrTick()
{
	if (--qrSecondsLeft_ <= 0) {
		EndQrLogin();
		return;
	}
	if (!qrScanned_)
		qrCountdown_->setText(Text("Login.QrExpiresIn").arg(FormatCountdown(qrSecondsLeft_)));
	// Every 2 seconds, as the API suggests, and only while the code is on
	// screen.
	if (qrSecondsLeft_ % 2 == 0 && pages_->currentIndex() == kQrPage)
		CheckQrLogin();
}

void LoginDialog::CheckQrLogin()
{
	if (!qrActive_ || qrChecking_)
		return;
	qrChecking_ = true;

	QPointer<LoginDialog> self = this;
	controller_.CheckQrLogin([self](QrLoginUpdate update) {
		if (!self)
			return;
		self->qrChecking_ = false;
		if (!self->qrActive_)
			return; // refreshed or ended meanwhile
		switch (update.result) {
		case QrLoginUpdate::Result::Waiting:
		case QrLoginUpdate::Result::NetworkError: // the next check may get through
			return;
		case QrLoginUpdate::Result::Scanned:
			self->qrScanned_ = true;
			self->qrCountdown_->setText(Text("Login.QrScanned"));
			return;
		case QrLoginUpdate::Result::Expired:
			self->EndQrLogin();
			return;
		case QrLoginUpdate::Result::NotEligible:
			self->EndQrLogin();
			ShowMessage(self, Text("Login.NotEligible"));
			return;
		case QrLoginUpdate::Result::UpdateRequired:
			self->EndQrLogin(Text("Login.UpdateRequired"));
			return;
		case QrLoginUpdate::Result::Failed:
			self->EndQrLogin(update.message.empty() ? Text("Login.QrFailed")
								: QString::fromStdString(update.message));
			return;
		case QrLoginUpdate::Result::LoggedIn:
			break;
		}
		self->qrActive_ = false;
		self->qrTimer_->stop();
		ShowMessage(self, Text("Login.Success").arg(QString::fromStdString(self->controller_.Nickname())));
		self->accept();
	});
}

void LoginDialog::EndQrLogin(const QString &message)
{
	qrActive_ = false;
	qrTimer_->stop();
	qrCountdown_->hide();
	qrCode_->SetExpired(true, message);
}

} // namespace langlive::obsplugin::ui
