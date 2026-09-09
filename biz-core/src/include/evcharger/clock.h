/**
 * @file clock.h
 * @brief 可注入的 UTC 时钟及可由测试推进的固定时钟。
 */

#ifndef EVCHARGER_CLOCK_H
#define EVCHARGER_CLOCK_H

#include <QDateTime>

namespace EvCharger {

	/** @brief 业务时钟抽象；通过手动依赖注入使过期和计量逻辑可确定地测试。 */
	class Clock {
	public:
		/**
		 * @brief 释放对象；虚析构保证通过接口指针销毁派生实例。
		 */
		virtual ~Clock() = default;
		/**
		 * @brief 获取当前时刻的 UTC 表示；固定时钟返回保存的测试时刻。
		 * @return 当前 UTC 时刻；实现不得自行推进业务状态。
		 */
		virtual QDateTime nowUtc() const = 0;
	};

	/** @brief 读取系统当前 UTC 时间的生产时钟。 */
	class SystemClock final : public Clock {
	public:
		/** @brief 获取系统或固定 UTC 时刻。 @return 当前时钟保存或读取的时间。 */
		QDateTime nowUtc() const override;
	};

	/** @brief 保存可变固定时刻的测试时钟；调用方负责同步并发读写。 */
	class FixedClock final : public Clock {
	public:
		/**
		 * @brief 保存调用方提供的固定时刻；调用方应传入 UTC 时间。
		 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
		 */
		explicit FixedClock(QDateTime nowUtc);

		/** @brief 获取系统或固定 UTC 时刻。 @return 当前时钟保存或读取的时间。 */
		QDateTime nowUtc() const override;
		/**
		 * @brief 替换固定时钟时刻，供测试模拟经过时间；调用方应传入 UTC 时间。
		 * @param nowUtc 用于过期判断、时间记录或即时计量的 UTC 时刻。
		 */
		void setNowUtc(QDateTime nowUtc);

	private:
		QDateTime m_nowUtc; ///< 固定时钟保存的 UTC 时刻。
	};

} // namespace EvCharger

#endif
