/**
 * @file core_tests.cpp
 * @brief 自动化测试：手机号与功率校验、时区解析、充电计量及球面距离。 使用 Qt Test 验证正常流程、校验失败与业务边界。
 */

#include "evcharger/charging.h"
#include "evcharger/clock.h"
#include "evcharger/geo.h"
#include "evcharger/validation.h"

#include <QJsonValue>
#include <QTest>

#include <limits>

/** @brief 领域输入校验、计量、时钟和地理工具的 Qt Test 测试集合。 */
class CoreTests : public QObject {
	Q_OBJECT

private slots:
	/**
	 * @brief 验证手机号 ASCII 数字约束及文本去空白后的长度边界。
	 */
	void validatesPhoneAndText();
	/**
	 * @brief 验证千瓦到整数瓦的精度、非法输入及功率上限。
	 */
	void parsesPowerWithoutFloatingPointDrift();
	/**
	 * @brief 验证显式时区输入转换为 UTC，并拒绝缺少时区的时间。
	 */
	void requiresExplicitTimeZone();
	/**
	 * @brief 验证充电时长、电量及金额独立采用 half-up 舍入。
	 */
	void calculatesChargeWithHalfUpRounding();
	/**
	 * @brief 验证时间倒退与整数溢出不会产生有效计量。
	 */
	void rejectsClockRegressionAndOverflow();
	/**
	 * @brief 验证同点距离及已知坐标间球面距离。
	 */
	void calculatesHaversineDistance();
	/**
	 * @brief 验证固定时钟可被显式推进而无需真实等待。
	 */
	void fixedClockCanAdvance();
};

/**
 * @brief 验证手机号 ASCII 数字约束及文本去空白后的长度边界。
 */
void CoreTests::validatesPhoneAndText() {
	QVERIFY(EvCharger::isPhoneNumber(QStringLiteral("13800138000")));
	QVERIFY(!EvCharger::isPhoneNumber(QStringLiteral("1380013800")));
	QVERIFY(!EvCharger::isPhoneNumber(QStringLiteral("1380013800a")));
	QVERIFY(!EvCharger::isPhoneNumber(QStringLiteral("１３８００１３８０００")));
	QString trimmed;
	QVERIFY(EvCharger::hasTrimmedLength(QStringLiteral("  用户  "), 1, 30, &trimmed));
	QCOMPARE(trimmed, QStringLiteral("用户"));
}

/**
 * @brief 验证千瓦到整数瓦的精度、非法输入及功率上限。
 */
void CoreTests::parsesPowerWithoutFloatingPointDrift() {
	QCOMPARE(EvCharger::parsePowerWatts(QJsonValue(120.125), 1'000'000), std::optional<qint64>(120'125));
	QVERIFY(!EvCharger::parsePowerWatts(QJsonValue(0.0001), 1'000'000).has_value());
	QVERIFY(!EvCharger::parsePowerWatts(QJsonValue(1000.001), 1'000'000).has_value());
	QVERIFY(!EvCharger::parsePowerWatts(QJsonValue(QStringLiteral("120")), 1'000'000).has_value());
}

/**
 * @brief 验证显式时区输入转换为 UTC，并拒绝缺少时区的时间。
 */
void CoreTests::requiresExplicitTimeZone() {
	const auto utc = EvCharger::parseExplicitRfc3339(QStringLiteral("2026-09-01T08:00:00+08:00"));
	QVERIFY(utc.has_value());
	QCOMPARE(*utc, QDateTime(QDate(2026, 9, 1), QTime(0, 0), Qt::UTC));
	QVERIFY(!EvCharger::parseExplicitRfc3339(QStringLiteral("2026-09-01T08:00:00")).has_value());
}

/**
 * @brief 验证充电时长、电量及金额独立采用 half-up 舍入。
 */
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

/**
 * @brief 验证时间倒退与整数溢出不会产生有效计量。
 */
void CoreTests::rejectsClockRegressionAndOverflow() {
	const QDateTime start(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC);
	QVERIFY(!EvCharger::calculateCharge(1000, 100, start, start.addSecs(-1)).has_value());
	QVERIFY(!EvCharger::calculateCharge(std::numeric_limits<qint64>::max(), 100, start, start.addSecs(2)).has_value());
	QVERIFY(!EvCharger::calculateCharge(1, std::numeric_limits<qint64>::max(), start, start.addSecs(1)).has_value());
}

/**
 * @brief 验证同点距离及已知坐标间球面距离。
 */
void CoreTests::calculatesHaversineDistance() {
	const double distance = EvCharger::haversineDistanceKm(38.889, 121.537, 38.901, 121.55);
	QVERIFY(distance > 1.70 && distance < 1.80);
	QCOMPARE(EvCharger::roundToHundredths(distance), 1.75);
}

/**
 * @brief 验证固定时钟可被显式推进而无需真实等待。
 */
void CoreTests::fixedClockCanAdvance() {
	const QDateTime initial(QDate(2026, 9, 1), QTime(8, 0), Qt::UTC);
	EvCharger::FixedClock clock(initial);
	QCOMPARE(clock.nowUtc(), initial);
	clock.setNowUtc(initial.addSecs(30));
	QCOMPARE(clock.nowUtc(), initial.addSecs(30));
}

QTEST_APPLESS_MAIN(CoreTests)

#include "core_tests.moc"
