// Exercise the production entry point, including QApplication::exec() and teardown.
#define main opsApplicationMain
#include "../src/main.cpp"
#undef main

#include "login-dialog/login_dialog.h"
#include "main-window/main_window.h"
#include <QPointer>
#include <QTimer>
#include <cstdlib>

namespace {
	bool completed = false;
	bool expired = false;
	QPointer<MainWindow> previousWindow;

	void fail(const char *message) {
		qCritical("FAIL: %s", message);
		std::_Exit(42);
	}

	void driveWindows() {
		auto *driver = new QTimer(qApp);
		QObject::connect(driver, &QTimer::timeout, qApp, [driver] {
			const auto scenario = QCoreApplication::arguments().value(1);
			auto *api = qApp->findChild<ops::ApiClient *>();
			if (!api || qApp->styleSheet().isEmpty())
				fail("application dependencies or stylesheet missing");
			// Never send test requests to a running development backend.
			api->setBaseUrl(QStringLiteral("http://127.0.0.1:1/api/v1"));
			for (auto *widget : QApplication::topLevelWidgets()) {
				if (!widget->isVisible())
					continue;
				if (auto *login = qobject_cast<LoginDialog *>(widget)) {
					if (expired && previousWindow)
						fail("expired session window was not destroyed");
					if (scenario == QLatin1String("login-close") ||
						(expired && scenario == QLatin1String("relogin-close"))) {
						completed = true;
						driver->stop();
						login->close();
					} else {
						// Drive the same success signal used by the asynchronous HTTP client.
						emit api->loginSucceeded(ops::AdminUser{});
					}
					return;
				}
				if (auto *window = qobject_cast<MainWindow *>(widget)) {
					if (!expired && scenario.startsWith(QLatin1String("relogin"))) {
						previousWindow = window;
						expired = true;
						emit api->authenticationChanged(false);
						emit api->authenticationChanged(false);
						return;
					}
					completed = true;
					driver->stop();
					if (scenario == QLatin1String("app-quit"))
						QCoreApplication::quit();
					else
						window->close();
					return;
				}
			}
		});
		driver->start(10);
		QTimer::singleShot(3000, qApp, [] {
			fail("closing the window did not terminate the application");
		});
	}
} // namespace
Q_COREAPP_STARTUP_FUNCTION(driveWindows)

int main(int argc, char **argv) {
	const int result = opsApplicationMain(argc, argv);
	if (!completed)
		fail("application exited before the scenario completed");
	return result;
}
