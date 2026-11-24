#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <QApplication>
#include <QStyleFactory>

void LoadTestLocale();

int main(int argc, char **argv)
{
	// No display needed. OBS's Windows Qt ships only the "minimal" headless
	// platform, not "offscreen".
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
#ifdef _WIN32
		qputenv("QT_QPA_PLATFORM", "minimal");
#else
		qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
	}

	QApplication app(argc, argv);
	QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	LoadTestLocale();

	doctest::Context context(argc, argv);
	return context.run();
}
