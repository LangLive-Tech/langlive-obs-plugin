#pragma once

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QWidget>

#include <functional>

class QPushButton;
class QSlider;

namespace langlive::obsplugin::ui {

// Square cover thumbnail, display only: the Upload Cover button next to it
// changes it. Shows a dashed "No cover uploaded" box when empty.
class CoverPicker : public QWidget {
	Q_OBJECT

public:
	explicit CoverPicker(QWidget *parent = nullptr);

	void SetImage(const QImage &image);
	const QImage &Image() const { return image_; }

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QImage image_;
};

// Pan and zoom an image inside a square frame. The visible square is the
// crop.
class CropView : public QWidget {
	Q_OBJECT

public:
	explicit CropView(const QImage &image, QWidget *parent = nullptr);

	// Zoom relative to the smallest scale that fills the square, 1.0+.
	void SetZoom(double zoom);
	QImage Crop(int outputSize) const;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

signals:
	void ZoomChanged(double zoom);

private:
	double Scale() const;
	void ClampOffset();

	QImage image_;
	double zoom_ = 1.0;
	QPointF center_;   // image point shown at the middle of the square
	QPointF dragFrom_; // widget position where the current drag started
};

// Lets the streamer pick the square part of an image, and uploads it with
// Upload Cover: accepted once the upload succeeded. A failed upload shows
// why, then the dialog closes (rejected) and the cover stays as it was.
class CoverCropDialog : public QDialog {
	Q_OBJECT

public:
	// Uploads the cropped cover; calls `done` with whether it was uploaded,
	// and if not, the message to show (none: say nothing, e.g. the login
	// ended and the menu tells the streamer).
	using Uploader = std::function<void(const QImage &cover, std::function<void(bool, QString)> done)>;

	CoverCropDialog(const QImage &image, Uploader uploader, QWidget *parent);

	QImage Result() const { return result_; }

	void reject() override; // not while uploading

private:
	void Upload();

	CropView *view_ = nullptr;
	QSlider *zoomSlider_ = nullptr;
	QPushButton *cancelButton_ = nullptr;
	QPushButton *uploadButton_ = nullptr;
	Uploader uploader_;
	bool uploading_ = false;
	QImage result_;
};

} // namespace langlive::obsplugin::ui
