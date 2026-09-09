/**
 * @file demo.h
 * @brief 根据进程环境变量调整演示动画的时长与按压幅度。
 */
#pragma once

#include <QtGlobal>

// 演示辅助：EV_DEMO_SLOW=1 时动效放慢 4 倍、按压放大，便于课堂讲解
namespace demo {
	// 是否处于慢速演示模式（进程启动时读取一次环境变量）
	/**
	 * @brief 读取并缓存演示慢速开关。
	 * @return EV_DEMO_SLOW 为非零整数时返回 true；环境只读取一次。
	 */
	inline bool slow() {
		static const bool value = qEnvironmentVariableIntValue("EV_DEMO_SLOW") != 0;
		return value;
	}

	// 动效时长换算：演示模式放慢 4 倍
	/**
	 * @brief 换算演示动画时长。
	 * @param base 常规动画时长，单位毫秒。
	 * @return 普通模式返回 base，慢速演示模式返回四倍时长。
	 */
	inline int ms(int base) {
		return slow() ? base * 4 : base;
	}

	// 按钮按压缩放幅度：演示模式更夸张
	/**
	 * @brief 返回按钮按压终点比例。
	 * @return 普通模式返回 0.97，慢速演示模式返回 0.90。
	 */
	inline qreal pressScale() {
		return slow() ? 0.90 : 0.97;
	}
} // namespace demo
