#pragma once

#include "langlive/obsplugin/core/validation.hpp"

#include <QDialog>

#include <vector>

class QPushButton;
class QRadioButton;

namespace langlive::obsplugin {
class PluginController;
}

namespace langlive::obsplugin::ui {

// Shown when the pre-stream check stopped a live: lists every problem and
// offers to apply the recommended settings and go live again (spec 4.7.2).
// Accepted once the live is started again.
class PreflightDialog : public QDialog {
	Q_OBJECT

public:
	PreflightDialog(PluginController &controller, std::vector<core::Violation> violations, QWidget *parent);

private:
	void OnApplyClicked();

	PluginController &controller_;
	std::vector<core::Violation> violations_;
	QRadioButton *generalRadio_ = nullptr;
	QRadioButton *highQualityRadio_ = nullptr;
	QPushButton *applyButton_ = nullptr;
};

} // namespace langlive::obsplugin::ui
