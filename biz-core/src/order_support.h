#ifndef BACKEND_ORDER_SUPPORT_H
#define BACKEND_ORDER_SUPPORT_H

#include <QDateTime>
#include <QJsonObject>

#include <optional>

class QSqlDatabase;

namespace Backend {

	bool loadOrderJson(QSqlDatabase &database, qint64 orderId, std::optional<qint64> userId, const QDateTime &nowUtc, QJsonObject *order, bool *found, QString *errorMessage);

} // namespace Backend

#endif
