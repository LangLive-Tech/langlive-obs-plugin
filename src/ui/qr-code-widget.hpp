#pragma once

#include <QWidget>

#include <string>
#include <vector>

class QLabel;
class QPushButton;

namespace langlive::obsplugin::ui {

// Draws a QR code on a white card. When expired, the code is dimmed and a
// refresh button is shown on top of it.
class QrCodeWidget : public QWidget {
	Q_OBJECT

public:
	explicit QrCodeWidget(QWidget *parent = nullptr);

	// Empty: a blank card, e.g. while the code is being created.
	void SetContent(const std::string &content);
	// Covers the code with `message` (default: "QR code expired") and a
	// refresh button.
	void SetExpired(bool expired, const QString &message = QString());

signals:
	void RefreshRequested();

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	void LayoutOverlay();

	int size_ = 0;              // modules per side
	std::vector<bool> modules_; // row-major, size_ * size_
	bool expired_ = false;
	QWidget *overlay_ = nullptr;
	QLabel *expiredLabel_ = nullptr;
	QPushButton *refreshButton_ = nullptr;
};

} // namespace langlive::obsplugin::ui
