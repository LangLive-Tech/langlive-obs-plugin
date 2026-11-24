#include "ui/preflight-dialog.hpp"

#include "app/plugin-controller.hpp"
#include "ui/go-live-dialog.hpp"
#include "ui/message-dialog.hpp"
#include "ui/style.hpp"
#include "ui/text.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <utility>

namespace langlive::obsplugin::ui {

namespace {

constexpr int kWidth = 560;

constexpr const char *kStyleSheet = R"(
	QLabel#preflightIntro { font-size: 15px; }
	QLabel#violation { font-size: 14px; }
)";

} // namespace

PreflightDialog::PreflightDialog(PluginController &controller, std::vector<core::Violation> violations, QWidget *parent)
	: QDialog(parent),
	  controller_(controller),
	  violations_(std::move(violations))
{
	ApplyDialogStyle(this, kStyleSheet);
	const Card cardParts = MakeCard(this, Text("Preflight.Title"), true);
	QFrame *card = cardParts.frame;
	QVBoxLayout *layout = cardParts.content;

	// Problems
	auto *body = new QVBoxLayout();
	body->setContentsMargins(28, 22, 28, 22);
	body->setSpacing(14);

	auto *intro = new QLabel(Text("Preflight.Intro"), card);
	intro->setObjectName(QStringLiteral("preflightIntro"));
	intro->setWordWrap(true);
	body->addWidget(intro);

	auto *list = new QVBoxLayout();
	list->setContentsMargins(8, 0, 0, 0);
	list->setSpacing(10);
	for (core::Violation violation : violations_) {
		auto *item = new QLabel(QStringLiteral("•  %1").arg(ViolationText(violation)), card);
		item->setObjectName(QStringLiteral("violation"));
		item->setWordWrap(true);
		list->addWidget(item);
	}
	body->addLayout(list);

	// With a wrong resolution the streamer picks the scenario to apply;
	// otherwise the current resolution already decides it.
	const bool chooseScenario = core::HasResolutionViolation(violations_);
	if (chooseScenario) {
		body->addSpacing(4);
		body->addWidget(MakeDivider(card));
	}
	body->addWidget(MakeHintLabel(Text(chooseScenario ? "Preflight.HintChooseScenario" : "Preflight.Hint"), card));

	if (chooseScenario) {
		body->addWidget(MakeFieldLabel(Text("Scenario.Title"), card));
		generalRadio_ = new QRadioButton(Text("Scenario.General"), card);
		highQualityRadio_ = new QRadioButton(Text("Scenario.HighQuality"), card);
		auto *cards = new QHBoxLayout();
		cards->setSpacing(12);
		auto *group = new QButtonGroup(card);
		for (QRadioButton *radio : {generalRadio_, highQualityRadio_}) {
			radio->setObjectName(QStringLiteral("scenarioCard"));
			radio->setCursor(Qt::PointingHandCursor);
			radio->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
			group->addButton(radio);
			cards->addWidget(radio, 1);
		}
		// Start from the scenario the streamer saved in the go-live dialog.
		(controller_.Settings().scenario == core::Scenario::HighQuality ? highQualityRadio_ : generalRadio_)
			->setChecked(true);
		body->addLayout(cards);
	}
	layout->addLayout(body);

	// Action
	layout->addWidget(MakeDivider(card));
	auto *footer = new QHBoxLayout();
	footer->setContentsMargins(28, 14, 28, 16);
	footer->addStretch();
	// Starts a live, so not a default button: Enter must not start it.
	applyButton_ = new QPushButton(Text("Preflight.ApplyAndStart"), card);
	applyButton_->setObjectName(QStringLiteral("primaryButton"));
	applyButton_->setCursor(Qt::PointingHandCursor);
	applyButton_->setAutoDefault(false);
	footer->addWidget(applyButton_);
	connect(applyButton_, &QPushButton::clicked, this, &PreflightDialog::OnApplyClicked);
	layout->addLayout(footer);

	FitCardHeight(this, kWidth);
}

void PreflightDialog::OnApplyClicked()
{
	core::Scenario scenario = controller_.CurrentScenario();
	if (highQualityRadio_)
		scenario = highQualityRadio_->isChecked() ? core::Scenario::HighQuality : core::Scenario::General;

	// Video and audio codecs other than H.264 / AAC are switched to x264 /
	// AAC by ApplySpec; the streamer asked for the fix with this button.
	applyButton_->setEnabled(false);
	applyButton_->setText(Text("GoLive.Starting"));
	QPointer<PreflightDialog> self = this;
	const auto restoreButton = [self]() {
		self->applyButton_->setEnabled(true);
		self->applyButton_->setText(Text("Preflight.ApplyAndStart"));
	};
	const bool applied = controller_.ApplySettingsAndStart(scenario, [self, restoreButton](StartLiveResult result) {
		if (!self)
			return;
		if (result.started()) {
			self->accept();
			return;
		}
		restoreButton();
		if (!result.violations.empty())
			ShowMessage(self, Text("Preflight.ApplyFailed"));
		else
			ShowStartLiveFailure(self, result);
	});
	if (!applied) {
		restoreButton();
		ShowMessage(this, Text("Preflight.ApplyFailed"));
	}
}

} // namespace langlive::obsplugin::ui
