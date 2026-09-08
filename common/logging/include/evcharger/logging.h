#pragma once

#include <QDebug>
#include <QLoggingCategory>
#include <QObject>
#include <QString>

namespace evcharger::logging {
	// Call once at process startup. Qt category filtering still happens at the call site.
	void installMessageHandler();
	QString objectLabel(const QObject *object);
	QString formatMessage(QtMsgType type, const QMessageLogContext &context, const QString &message);
} // namespace evcharger::logging

#define EV_LOG_DEBUG(category, object) qCDebug(category).noquote() << evcharger::logging::objectLabel(object)
#define EV_LOG_INFO(category, object) qCInfo(category).noquote() << evcharger::logging::objectLabel(object)
#define EV_LOG_WARNING(category, object) qCWarning(category).noquote() << evcharger::logging::objectLabel(object)
#define EV_LOG_CRITICAL(category, object) qCCritical(category).noquote() << evcharger::logging::objectLabel(object)
