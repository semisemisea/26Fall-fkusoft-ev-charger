#include "evcharger/logging.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <cstdio>

namespace evcharger::logging {
	namespace {
		QString singleLine(QString text) {
			text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
			text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
			text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
			text.replace(QLatin1Char('\t'), QStringLiteral("\\t"));
			text.replace(QChar(0), QStringLiteral("\\0"));
			return text;
		}

		const char *levelName(QtMsgType type) {
			switch (type) {
			case QtDebugMsg:
				return "DEBUG";
			case QtInfoMsg:
				return "INFO";
			case QtWarningMsg:
				return "WARN";
			case QtCriticalMsg:
				return "ERROR";
			case QtFatalMsg:
				return "FATAL";
			}
			return "UNKNOWN";
		}

		void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
			// Do not use Qt logging here: doing so would recursively invoke the handler.
			static QMutex outputMutex;
			const QByteArray output = (formatMessage(type, context, message) + QLatin1Char('\n')).toUtf8();
			QMutexLocker lock(&outputMutex);
			std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stderr);
			std::fflush(stderr);
		}
	} // namespace

	QString objectLabel(const QObject *object) {
		const QString name = object ? object->objectName() : QString();
		return QStringLiteral("[object=%1]").arg(name.isEmpty() ? QStringLiteral("-") : name);
	}

	QString formatMessage(QtMsgType type, const QMessageLogContext &context, const QString &message) {
		const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
		const QString threadId = QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);
		const QString threadName = QThread::currentThread()->objectName();
		const QString category = QString::fromUtf8(context.category ? context.category : "default");
		const QString function = QString::fromUtf8(context.function ? context.function : "-");
		QString result = QStringLiteral("[%1] [%2] [%3] [thread=0x%4 name=%5] [%6] %7")
							 .arg(timestamp, QString::fromLatin1(levelName(type)), singleLine(category), threadId,
								  singleLine(threadName.isEmpty() ? QStringLiteral("-") : threadName), singleLine(function),
								  singleLine(message.startsWith(QStringLiteral("[object=")) ? message : QStringLiteral("[object=-] ") + message));
		if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && context.file) {
			result += QStringLiteral(" | source=%1:%2").arg(singleLine(QString::fromUtf8(context.file))).arg(context.line);
		}
		return result;
	}

	void installMessageHandler() {
		QThread::currentThread()->setObjectName(QStringLiteral("main"));
		qInstallMessageHandler(messageHandler);
	}
} // namespace evcharger::logging
