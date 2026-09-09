#include "route_playback.h"

#include <QJsonArray>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
	double meters(QPointF a, QPointF b) {
		const auto lat = qDegreesToRadians(b.x() - a.x());
		const auto lon = qDegreesToRadians(b.y() - a.y());
		const auto h = std::pow(std::sin(lat / 2), 2) + std::cos(qDegreesToRadians(a.x())) * std::cos(qDegreesToRadians(b.x())) * std::pow(std::sin(lon / 2), 2);
		return 12742000 * std::asin(std::sqrt(std::clamp(h, 0.0, 1.0)));
	}
} // namespace

bool RoutePlayback::load(const QJsonObject &route) {
	*this = RoutePlayback{};
	const auto points = route.value(QStringLiteral("polyline")).toArray();
	m_distance = route.value(QStringLiteral("distanceM")).toDouble(-1);
	m_duration = route.value(QStringLiteral("durationSec")).toDouble(-1);
	if (!std::isfinite(m_distance) || !std::isfinite(m_duration) || m_distance < 0 || m_duration <= 0 || points.size() < 2)
		return false;
	for (const auto &value : points) {
		const auto pair = value.toArray();
		if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble()) {
			*this = RoutePlayback{};
			return false;
		}
		const QPointF point(pair[0].toDouble(), pair[1].toDouble());
		if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || std::abs(point.x()) > 90 || std::abs(point.y()) > 180) {
			*this = RoutePlayback{};
			return false;
		}
		m_lengths.append(m_points.isEmpty() ? 0 : m_lengths.last() + meters(m_points.last(), point));
		m_points.append(point);
	}
	if (m_lengths.last() <= 0) {
		*this = RoutePlayback{};
		return false;
	}
	double end = 0;
	for (const auto &value : route.value(QStringLiteral("steps")).toArray()) {
		const auto step = value.toObject();
		const auto instruction = step.value(QStringLiteral("instruction")).toString().trimmed();
		const auto distance = step.value(QStringLiteral("distanceM")).toDouble();
		if (instruction.isEmpty() || !std::isfinite(distance) || distance < 0)
			continue;
		end += distance;
		m_instructions.append(instruction);
		m_stepEnds.append(end);
	}
	return true;
}

void RoutePlayback::reset() { m_elapsed = 0; }
void RoutePlayback::advance(double seconds) {
	if (ready() && std::isfinite(seconds) && seconds > 0)
		m_elapsed = std::min(m_duration, m_elapsed + seconds);
}

QPointF RoutePlayback::position() const {
	if (!ready())
		return {};
	const auto distance = m_lengths.last() * m_elapsed / m_duration;
	const auto it = std::upper_bound(m_lengths.cbegin(), m_lengths.cend(), distance);
	if (it == m_lengths.cend())
		return m_points.last();
	const auto index = int(it - m_lengths.cbegin());
	const auto fraction = (distance - m_lengths[index - 1]) / (m_lengths[index] - m_lengths[index - 1]);
	return m_points[index - 1] + fraction * (m_points[index] - m_points[index - 1]);
}

double RoutePlayback::remainingMeters() const {
	return ready() ? m_distance * (1 - m_elapsed / m_duration) : 0;
}

int RoutePlayback::stepIndex() const {
	if (!ready() || m_stepEnds.isEmpty())
		return -1;
	const auto distance = m_stepEnds.last() * m_elapsed / m_duration;
	return std::min(int(std::upper_bound(m_stepEnds.cbegin(), m_stepEnds.cend(), distance) - m_stepEnds.cbegin()), int(m_stepEnds.size() - 1));
}
