/**
 * @file StationCard.h
 * @brief 展示单个电站摘要，并区分卡片选择与导航点击。
 */
#pragma once

#include "models/Station.h"

#include <QFrame>

class QLabel;

/**
 * @brief 电站卡片：列表中的单个电站条目（名称/地址/单价/空闲数/距离），点击发 clicked 信号
 * @details 子控件、布局和动画通过 Qt 父子树管理；外部仅需管理本控件的生命周期。
 */
class StationCard : public QFrame {
	Q_OBJECT

public:
	/**
	 * @brief 复制电站快照，创建摘要标签并安装导航点击过滤器。
	 * @param station 目标电站的界面模型。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit StationCard(const Station &station, QWidget *parent = nullptr);

	/**
	 * @brief 读取卡片持有的电站快照。
	 * @return 卡片持有的电站快照的只读引用。
	 */
	[[nodiscard]] const Station &station() const { return m_station; } ///< 当前展示或导航的电站快照。
	/**
	 * @brief 搜索过滤：名称或地址包含关键字（不区分大小写）
	 * @param filter 按名称或地址匹配的搜索关键字。
	 * @return 关键字为空或名称、地址包含关键字时为 true。
	 */
	[[nodiscard]] bool matches(const QString &filter) const;

signals:
	/**
	 * @brief 卡片主体收到有效左键释放时发出当前电站快照
	 * @param station 目标电站的界面模型。
	 */
	void clicked(const Station &station);
	/**
	 * @brief 用户点击距离区域时请求导航到当前电站。
	 * @param station 目标电站的界面模型。
	 */
	void navigateRequested(const Station &station);

protected:
	/**
	 * @brief 卡片范围内松开鼠标视为点击
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void mouseReleaseEvent(QMouseEvent *event) override;
	/**
	 * @brief 拦截距离标签的点击，转发为导航请求
	 * @param watched 被安装事件过滤器的对象，所有权保持不变。
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 * @return 事件已由本控件处理时为 true，否则沿用基类过滤结果。
	 */
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	Station m_station;				   ///< 当前展示或导航的电站快照。
	QLabel *m_distanceLabel = nullptr; ///< 站点距离标签；卡片中同时作为导航点击区域。
};
