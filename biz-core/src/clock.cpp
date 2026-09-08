/**
 * @file clock.cpp
 * @brief 可注入的 UTC 时钟及可由测试推进的固定时钟。
 */

#include "evcharger/clock.h"

#include <utility>

namespace EvCharger {

	/**
	 * @brief 获取当前时刻的 UTC 表示；固定时钟返回保存的测试时刻。
	 * @return 以 UTC 表示的系统时刻或固定时钟时刻。
	 */
	QDateTime SystemClock::nowUtc() const {
		return QDateTime::currentDateTimeUtc();
	}

	/**
	 * @brief 保存调用方提供的固定时刻；调用方应传入 UTC 时间。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 */
	FixedClock::FixedClock(QDateTime nowUtc)
		: m_nowUtc(std::move(nowUtc)) {
	}

	/**
	 * @brief 获取当前时刻的 UTC 表示；固定时钟返回保存的测试时刻。
	 * @return 以 UTC 表示的系统时刻或固定时钟时刻。
	 */
	QDateTime FixedClock::nowUtc() const {
		return m_nowUtc;
	}

	/**
	 * @brief 替换固定时钟时刻，供测试模拟经过时间；调用方应传入 UTC 时间。
	 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
	 */
	void FixedClock::setNowUtc(QDateTime nowUtc) {
		m_nowUtc = std::move(nowUtc);
	}

} // namespace EvCharger
