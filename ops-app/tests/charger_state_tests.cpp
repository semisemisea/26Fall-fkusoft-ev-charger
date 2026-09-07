#include "api/types.h"

#include <QJsonObject>
#include <QTest>

class ChargerStateTests : public QObject {
	Q_OBJECT

private slots:
	void preservesIndependentStates();
	void restartRequiresAvailableFaultOrOffline();
};

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
