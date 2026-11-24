#include "ui/qr-code-widget.hpp"

#include "ui/text.hpp"

#include <qrcodegen.hpp>

#include <algorithm>
#include <cmath>

#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QVBoxLayout>

namespace langlive::obsplugin::ui {

namespace {

// White margin around the code, in modules. The QR spec asks for 4, but the
// white card around it already sets it apart from the dark dialog, and 2
// leave the code more room; phone cameras read it fine.
constexpr int kQuietZone = 2;
constexpr int kCornerRadius = 12;

} // namespace

QrCodeWidget::QrCodeWidget(QWidget *parent) : QWidget(parent)
{
	setFixedSize(200, 200);

	overlay_ = new QWidget(this);
	overlay_->setObjectName(QStringLiteral("qrOverlay"));
	auto *layout = new QVBoxLayout(overlay_);
	layout->setAlignment(Qt::AlignCenter);
	layout->setSpacing(10);

	expiredLabel_ = new QLabel(Text("Login.QrExpired"), overlay_);
	expiredLabel_->setObjectName(QStringLiteral("qrExpiredLabel"));
	expiredLabel_->setAlignment(Qt::AlignCenter);
	expiredLabel_->setWordWrap(true); // longer messages than "expired"
	expiredLabel_->setContentsMargins(12, 0, 12, 0);
	layout->addWidget(expiredLabel_);

	refreshButton_ = new QPushButton(Text("Login.QrRefresh"), overlay_);
	refreshButton_->setObjectName(QStringLiteral("primaryButton"));
	refreshButton_->setCursor(Qt::PointingHandCursor);
	layout->addWidget(refreshButton_, 0, Qt::AlignCenter);

	overlay_->hide();
	connect(refreshButton_, &QPushButton::clicked, this, &QrCodeWidget::RefreshRequested);
}

void QrCodeWidget::SetContent(const std::string &content)
{
	if (content.empty()) {
		size_ = 0;
		modules_.clear();
		update();
		return;
	}
	const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(content.c_str(), qrcodegen::QrCode::Ecc::LOW);
	size_ = code.getSize();
	modules_.assign(static_cast<size_t>(size_ * size_), false);
	for (int y = 0; y < size_; ++y) {
		for (int x = 0; x < size_; ++x)
			modules_[static_cast<size_t>(y * size_ + x)] = code.getModule(x, y);
	}
	update();
}

void QrCodeWidget::SetExpired(bool expired, const QString &message)
{
	expired_ = expired;
	expiredLabel_->setText(message.isEmpty() ? Text("Login.QrExpired") : message);
	overlay_->setVisible(expired);
	update();
}

void QrCodeWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	// Always white, whatever the OBS theme: phone cameras need contrast.
	// The thin border keeps the card visible on light themes.
	painter.setPen(QPen(QColor(0, 0, 0, 30), 1));
	painter.setBrush(Qt::white);
	painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), kCornerRadius, kCornerRadius);
	painter.setPen(Qt::NoPen);

	if (size_ > 0) {
		// The code fills the card but its quiet zone. Module edges are
		// rounded to device pixels, so modules differ by at most one
		// pixel, with no gaps between them and sharp edges.
		const qreal ratio = devicePixelRatioF();
		const qreal side = std::min(width(), height());
		const qreal module = side / (size_ + 2 * kQuietZone);
		const qreal originX = (width() - size_ * module) / 2;
		const qreal originY = (height() - size_ * module) / 2;
		const auto edge = [&](qreal origin, int index) {
			return std::round((origin + index * module) * ratio) / ratio;
		};

		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.setBrush(Qt::black);
		for (int y = 0; y < size_; ++y) {
			for (int x = 0; x < size_; ++x) {
				if (modules_[static_cast<size_t>(y * size_ + x)])
					painter.drawRect(QRectF(QPointF(edge(originX, x), edge(originY, y)),
								QPointF(edge(originX, x + 1), edge(originY, y + 1))));
			}
		}
	}

	if (expired_) {
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setBrush(QColor(255, 255, 255, 225));
		painter.drawRoundedRect(rect(), kCornerRadius, kCornerRadius);
	}
}

void QrCodeWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	LayoutOverlay();
}

void QrCodeWidget::LayoutOverlay()
{
	overlay_->setGeometry(rect());
}

} // namespace langlive::obsplugin::ui
