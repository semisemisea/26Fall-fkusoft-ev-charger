#include "evcharger/charging.h"
#include "evcharger/clock.h"
#include "evcharger/geo.h"
#include "evcharger/validation.h"

#include <QJsonValue>
#include <QTest>

#include <limits>

class CoreTests : public QObject {
	Q_OBJECT

private slots:
	void validatesPhoneAndText();
	void parsesPowerWithoutFloatingPointDrift();
	void requiresExplicitTimeZone();
	void calculatesChargeWithHalfUpRounding();
	void rejectsClockRegressionAndOverflow();
	void calculatesHaversineDistance();
	void fixedClockCanAdvance();
};

void CoreTests::validatesPhoneAndText() {
	QVERIFY(EvCharger::isPhoneNumber(QStringLiteral("13800138000")));
	QVERIFY(!EvCharger::isPhoneNumber(QStringLiteral("1380013800")));
	QVERIFY(!EvCharger::isPhoneNumber(QStringLiteral("1380013800a")));
	QVERIFY(!EvCharger::isPhoneNumber(QStringLiteral("１３８００１３８０００")));
	QString trimmed;
	QVERIFY(EvCharger::hasTrimmedLength(QStringLiteral("  用户  "), 1, 30, &trimmed));
	QCOMPARE(trimmed, QStringLiteral("用户"));
}

void CoreTests::parsesPowerWithoutFloatingPointDrift() {
	QCOMPARE(EvCharger::parsePowerWatts(QJsonValue(120.125), 1'000'000), std::optional<qint64>(120'125));
	QVERIFY(!EvCharger::parsePowerWatts(QJsonValue(0.0001), 1'000'000).has_value());
	QVERIFY(!EvCharger::parsePowerWatts(QJsonValue(1000.001), 1'000'000).has_value());
	QVERIFY(!EvCharger::parsePowerWatts(QJsonValue(QStringLiteral("120")), 1'000'000).has_value());
}

void CoreTests::requiresExplicitTimeZone() {
	const auto utc = EvCharger::parseExplicitRfc3339(QStringLiteral("2026-09-01T08:00:00+08:00"));
	QVERIFY(utc.has_value());
	QCOMPARE(*utc, QDateTime(QDate(2026, 9, 1), QTime(0, 0), Qt::UTC));
	QVERIFY(!EvCharger::parseExplicitRfc3339(QStringLiteral("2026-09-01T08:00:00")).has_value());
}

void CoreTests::calculatesChargeWithHalfUpRounding() {
	const QDateTime start(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC);
	const auto metrics = EvCharger::calculateCharge(1000, 1800, start, start.addSecs(1));
	QVERIFY(metrics.has_value());
	QCOMPARE(metrics->durationSeconds, qint64(1));
	QCOMPARE(metrics->energyWs, qint64(1000));
	QCOMPARE(metrics->amountFen, qint64(1));
	QCOMPARE(metrics->energyHundredthsKwh, qint64(0));

	const auto energyRoundsUp = EvCharger::calculateCharge(18'000, 1, start, start.addSecs(1));
	QVERIFY(energyRoundsUp.has_value());
	QCOMPARE(energyRoundsUp->energyHundredthsKwh, qint64(1));
}

void CoreTests::rejectsClockRegressionAndOverflow() {
	const QDateTime start(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC);
	QVERIFY(!EvCharger::calculateCharge(1000, 100, start, start.addSecs(-1)).has_value());
	QVERIFY(!EvCharger::calculateCharge(std::numeric_limits<qint64>::max(), 100, start, start.addSecs(2)).has_value());
	QVERIFY(!EvCharger::calculateCharge(1, std::numeric_limits<qint64>::max(), start, start.addSecs(1)).has_value());
}

void CoreTests::calculatesHaversineDistance() {
	const double distance = EvCharger::haversineDistanceKm(38.889, 121.537, 38.901, 121.55);
	QVERIFY(distance > 1.70 && distance < 1.80);
	QCOMPARE(EvCharger::roundToHundredths(distance), 1.75);
}

void CoreTests::fixedClockCanAdvance() {
	const QDateTime initial(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC);
	EvCharger::FixedClock clock(initial);
	QCOMPARE(clock.nowUtc(), initial);
	clock.setNowUtc(initial.addSecs(30));
	QCOMPARE(clock.nowUtc(), initial.addSecs(30));
}

QTEST_APPLESS_MAIN(CoreTests)

#include "core_tests.moc"
