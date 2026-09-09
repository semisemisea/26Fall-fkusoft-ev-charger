#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QStringList>
#include <QVector>

/// 应用内模拟路线；坐标以 QPointF(纬度, 经度) 保存，不代表真实定位。
class RoutePlayback {
public:
	bool load(const QJsonObject &route);
	void reset();
	void advance(double seconds);
	[[nodiscard]] bool ready() const { return m_points.size() >= 2; }
	[[nodiscard]] bool finished() const { return ready() && m_elapsed >= m_duration; }
	[[nodiscard]] QPointF position() const;
	[[nodiscard]] double remainingMeters() const;
	[[nodiscard]] int stepIndex() const;
	[[nodiscard]] const QStringList &instructions() const { return m_instructions; }

private:
	QVector<QPointF> m_points;
	QVector<double> m_lengths;
	QVector<double> m_stepEnds;
	QStringList m_instructions;
	double m_distance = 0;
	double m_duration = 0;
	double m_elapsed = 0;
};
