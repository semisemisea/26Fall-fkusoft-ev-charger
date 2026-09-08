#include "evcharger/logging.h"

#include <QRegularExpression>
#include <QTest>
#include <QThread>

Q_LOGGING_CATEGORY(testLogging, "evcharger.test")

namespace {
	QString capturedMessage;
	QString capturedFunction;
	void capture(QtMsgType, const QMessageLogContext &context, const QString &message) {
		capturedMessage = message;
		capturedFunction = QString::fromUtf8(context.function);
	}
} // namespace

class LoggingTests : public QObject {
	Q_OBJECT
private slots:
	void formatIncludesContext() {
		QThread::currentThread()->setObjectName(QStringLiteral("test-thread"));
		const QMessageLogContext context("example.cpp", 42, "Service::start()", "evcharger.test");
		const auto text = evcharger::logging::formatMessage(QtCriticalMsg, context, "[object=charger-03] Start failed");
		QVERIFY(QRegularExpression(QStringLiteral("^\\[\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3}Z\\] ")).match(text).hasMatch());
		QVERIFY(text.contains("[ERROR] [evcharger.test] [thread=0x"));
		QVERIFY(text.contains("name=test-thread] [Service::start()] [object=charger-03] Start failed"));
		QVERIFY(text.endsWith("source=example.cpp:42"));
	}
	void missingContextAndMultiline() {
		const auto text = evcharger::logging::formatMessage(QtWarningMsg, QMessageLogContext(), "Failure\r\nfor\titem");
		QVERIFY(text.contains("[object=-] Failure\\r\\nfor\\titem"));
		QVERIFY(!text.contains('\n'));
		QVERIFY(!text.contains('\r'));
		QVERIFY(text.contains("[-]"));
		QCOMPARE(evcharger::logging::objectLabel(nullptr), QStringLiteral("[object=-]"));
	}
	void categoriesFilterAtCallSite() {
		QObject object;
		object.setObjectName("test-object");
		auto previous = qInstallMessageHandler(capture);
		QLoggingCategory::setFilterRules("evcharger.test.info=false");
		capturedMessage.clear();
		int evaluated = 0;
		EV_LOG_INFO(testLogging, &object) << ++evaluated;
		const bool filtered = capturedMessage.isEmpty() && evaluated == 0;
		QLoggingCategory::setFilterRules("evcharger.test.info=true");
		EV_LOG_INFO(testLogging, &object) << "Operation completed";
		qInstallMessageHandler(previous);
		QLoggingCategory::setFilterRules(QString());
		QVERIFY(filtered);
		QCOMPARE(capturedMessage, QStringLiteral("[object=test-object] Operation completed"));
		QVERIFY(capturedFunction.contains("categoriesFilterAtCallSite"));
	}
};

QTEST_GUILESS_MAIN(LoggingTests)
#include "logging_tests.moc"
