#include "ui/cover-picker.hpp"

#include "ui/message-dialog.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace langlive::obsplugin::ui {

namespace {

constexpr int kThumbnailSize = 160;
constexpr int kCropViewSize = 320;
constexpr double kMaxZoom = 4.0;
constexpr int kZoomSliderSteps = 100;
constexpr qreal kCornerRadius = 8.0;
// Output side length; the spec recommends more than 400 x 400.
constexpr int kCoverOutputSize = 800;

} // namespace

// --- CoverPicker ---------------------------------------------------------------

CoverPicker::CoverPicker(QWidget *parent) : QWidget(parent)
{
	setFixedSize(kThumbnailSize, kThumbnailSize);
}

void CoverPicker::SetImage(const QImage &image)
{
	image_ = image;
	update();
}

void CoverPicker::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

	if (!image_.isNull()) {
		QPainterPath clip;
		clip.addRoundedRect(box, kCornerRadius, kCornerRadius);
		painter.setClipPath(clip);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		painter.drawImage(box, image_);
		if (!isEnabled())
			painter.fillRect(box, QColor(0, 0, 0, 90));
		return;
	}

	QColor line = palette().color(QPalette::PlaceholderText);
	if (!isEnabled())
		line.setAlpha(90);
	painter.setPen(QPen(line, 1, Qt::DashLine));
	painter.setBrush(Qt::NoBrush);
	painter.drawRoundedRect(box, kCornerRadius, kCornerRadius);

	painter.setPen(line);
	QFont textFont = font();
	textFont.setPixelSize(13);
	painter.setFont(textFont);
	painter.drawText(rect(), Qt::AlignCenter, Text("GoLive.NoCover"));
}

// --- CropView ------------------------------------------------------------------

CropView::CropView(const QImage &image, QWidget *parent)
	: QWidget(parent),
	  image_(image),
	  center_(image.width() / 2.0, image.height() / 2.0)
{
	setFixedSize(kCropViewSize, kCropViewSize);
	setCursor(Qt::OpenHandCursor);
}

double CropView::Scale() const
{
	// The smallest scale that still fills the square, times the zoom.
	const double fill = std::max(double(width()) / image_.width(), double(height()) / image_.height());
	return fill * zoom_;
}

void CropView::ClampOffset()
{
	// Keep the square fully covered by the image.
	const double halfVisible = width() / (2.0 * Scale());
	center_.setX(std::clamp(center_.x(), halfVisible, image_.width() - halfVisible));
	center_.setY(std::clamp(center_.y(), halfVisible, image_.height() - halfVisible));
}

void CropView::SetZoom(double zoom)
{
	zoom_ = std::clamp(zoom, 1.0, kMaxZoom);
	ClampOffset();
	update();
}

QImage CropView::Crop(int outputSize) const
{
	QImage result(outputSize, outputSize, QImage::Format_RGB32);
	result.fill(Qt::black);
	QPainter painter(&result);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	const double scale = Scale() * outputSize / width();
	painter.translate(outputSize / 2.0, outputSize / 2.0);
	painter.scale(scale, scale);
	painter.translate(-center_);
	painter.drawImage(QPointF(0, 0), image_);
	return result;
}

void CropView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.fillRect(rect(), Qt::black);

	painter.save();
	painter.translate(width() / 2.0, height() / 2.0);
	painter.scale(Scale(), Scale());
	painter.translate(-center_);
	painter.drawImage(QPointF(0, 0), image_);
	painter.restore();

	// Rule-of-thirds guides help framing.
	painter.setPen(QPen(QColor(255, 255, 255, 70), 1));
	for (int i = 1; i < 3; ++i) {
		painter.drawLine(QPointF(width() * i / 3.0, 0), QPointF(width() * i / 3.0, height()));
		painter.drawLine(QPointF(0, height() * i / 3.0), QPointF(width(), height() * i / 3.0));
	}
	painter.setPen(QPen(QColor(255, 255, 255, 200), 1));
	painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}

void CropView::mousePressEvent(QMouseEvent *event)
{
	dragFrom_ = event->position();
	setCursor(Qt::ClosedHandCursor);
}

void CropView::mouseMoveEvent(QMouseEvent *event)
{
	if (!(event->buttons() & Qt::LeftButton))
		return;
	const QPointF delta = event->position() - dragFrom_;
	dragFrom_ = event->position();
	center_ -= delta / Scale();
	ClampOffset();
	update();
}

void CropView::wheelEvent(QWheelEvent *event)
{
	const double steps = event->angleDelta().y() / 120.0;
	SetZoom(zoom_ * std::pow(1.1, steps));
	emit ZoomChanged(zoom_);
}

// --- CoverCropDialog -------------------------------------------------------------

CoverCropDialog::CoverCropDialog(const QImage &image, Uploader uploader, QWidget *parent)
	: QDialog(parent),
	  uploader_(std::move(uploader))
{
	setWindowTitle(Text("Cover.Title"));
	ApplyDialogStyle(this);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(24, 20, 24, 20);
	layout->setSpacing(12);

	layout->addWidget(MakeHintLabel(Text("Cover.Hint"), this));

	view_ = new CropView(image, this);
	layout->addWidget(view_, 0, Qt::AlignHCenter);

	auto *zoomRow = new QHBoxLayout();
	zoomRow->addWidget(MakeHintLabel(Text("Cover.Zoom"), this));
	zoomSlider_ = new QSlider(Qt::Horizontal, this);
	zoomSlider_->setRange(0, kZoomSliderSteps);
	zoomRow->addWidget(zoomSlider_, 1);
	layout->addLayout(zoomRow);

	auto *buttons = new QHBoxLayout();
	buttons->addStretch();
	cancelButton_ = new QPushButton(Text("Cover.Cancel"), this);
	cancelButton_->setObjectName(QStringLiteral("secondaryButton"));
	uploadButton_ = new QPushButton(Text("Cover.Upload"), this);
	uploadButton_->setObjectName(QStringLiteral("primaryButton"));
	uploadButton_->setDefault(true);
	buttons->addWidget(cancelButton_);
	buttons->addWidget(uploadButton_);
	layout->addLayout(buttons);

	auto sliderToZoom = [](int value) {
		return 1.0 + (kMaxZoom - 1.0) * value / kZoomSliderSteps;
	};
	connect(zoomSlider_, &QSlider::valueChanged, this,
		[this, sliderToZoom](int value) { view_->SetZoom(sliderToZoom(value)); });
	connect(view_, &CropView::ZoomChanged, this, [this](double zoom) {
		const QSignalBlocker blocker(zoomSlider_);
		zoomSlider_->setValue(int(std::lround((zoom - 1.0) / (kMaxZoom - 1.0) * kZoomSliderSteps)));
	});
	connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
	connect(uploadButton_, &QPushButton::clicked, this, &CoverCropDialog::Upload);
}

void CoverCropDialog::reject()
{
	if (!uploading_)
		QDialog::reject();
}

void CoverCropDialog::Upload()
{
	if (uploading_)
		return;
	uploading_ = true;
	result_ = view_->Crop(kCoverOutputSize);
	for (QWidget *widget : {static_cast<QWidget *>(view_), static_cast<QWidget *>(zoomSlider_),
				static_cast<QWidget *>(cancelButton_), static_cast<QWidget *>(uploadButton_)})
		widget->setEnabled(false);
	uploadButton_->setText(Text("Cover.Uploading"));

	QPointer<CoverCropDialog> self = this;
	uploader_(result_, [self](bool uploaded, QString message) {
		if (!self)
			return;
		self->uploading_ = false;
		if (uploaded) {
			self->accept();
			return;
		}
		if (!message.isEmpty())
			ShowMessage(self, message);
		self->result_ = QImage();
		self->QDialog::reject(); // the cover stays as it was
	});
}

} // namespace langlive::obsplugin::ui
