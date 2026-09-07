#include "evcharger/clock.h"

#include <utility>

namespace EvCharger {

	QDateTime SystemClock::nowUtc() const {
		return QDateTime::currentDateTimeUtc();
	}

	FixedClock::FixedClock(QDateTime nowUtc)
		: m_nowUtc(std::move(nowUtc)) {
	}

	QDateTime FixedClock::nowUtc() const {
		return m_nowUtc;
	}

	void FixedClock::setNowUtc(QDateTime nowUtc) {
		m_nowUtc = std::move(nowUtc);
	}

} // namespace EvCharger
