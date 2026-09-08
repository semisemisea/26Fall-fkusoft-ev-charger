/** @file
 * @brief 管理员前端回归测试；用本地响应或直接发送信号隔离真实服务。
 */
#include "api/types.h"

#include <QJsonObject>
#include <QTest>

/// @brief 独立状态解析与界面重启条件回归测试。
class ChargerStateTests : public QObject {
	Q_OBJECT

private slots:
	/// @brief 验证 charging 与 fault 可同时存在，展示时保留两种状态。
	void preservesIndependentStates();
	/// @brief 验证闲置故障和闲置离线可重启，在用或正常在线不可重启。
	void restartRequiresAvailableFaultOrOffline();
};

/// @brief 验证 charging 与 fault 可同时存在，展示时保留两种状态。

void ChargerStateTests::preservesIndependentStates() {
	const ops::Charger charger = ops::chargerFromJson(QJsonObject{
		{QStringLiteral("id"), 17},
		{QStringLiteral("stationId"), 3},
		{QStringLiteral("type"), QStringLiteral("fast")},
		{QStringLiteral("powerKw"), 120.0},
		{QStringLiteral("occupancyStatus"), QStringLiteral("charging")},
		{QStringLiteral("operationalStatus"), QStringLiteral("fault")},
	});

	QCOMPARE(charger.occupancyStatus, QStringLiteral("charging"));
	QCOMPARE(charger.operationalStatus, QStringLiteral("fault"));
	QCOMPARE(ops::chargerStatusText(charger), QStringLiteral("在用 / 故障"));
}

/// @brief 验证闲置故障和闲置离线可重启，在用或正常在线不可重启。

void ChargerStateTests::restartRequiresAvailableFaultOrOffline() {
	ops::Charger charger;
	charger.occupancyStatus = QStringLiteral("available");
	charger.operationalStatus = QStringLiteral("fault");
	QVERIFY(ops::isRestartable(charger));

	charger.operationalStatus = QStringLiteral("offline");
	QVERIFY(ops::isRestartable(charger));

	charger.occupancyStatus = QStringLiteral("charging");
	QVERIFY(!ops::isRestartable(charger));

	charger.occupancyStatus = QStringLiteral("available");
	charger.operationalStatus = QStringLiteral("online");
	QVERIFY(!ops::isRestartable(charger));
	QCOMPARE(ops::chargerStatusText(charger), QStringLiteral("闲置"));
}

QTEST_APPLESS_MAIN(ChargerStateTests)

#include "charger_state_tests.moc"
