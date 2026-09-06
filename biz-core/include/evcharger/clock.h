#ifndef EVCHARGER_CLOCK_H
#define EVCHARGER_CLOCK_H

#include <QDateTime>

namespace EvCharger {

	class Clock {
	public:
		virtual ~Clock() = default;
		virtual QDateTime nowUtc() const = 0;
	};

	class SystemClock final : public Clock {
	public:
		QDateTime nowUtc() const override;
	};

	class FixedClock final : public Clock {
	public:
		explicit FixedClock(QDateTime nowUtc);

		QDateTime nowUtc() const override;
		void setNowUtc(QDateTime nowUtc);

	private:
		QDateTime m_nowUtc;
	};

} // namespace EvCharger

#endif
