/** @file
 * @brief 双维度电桩统计页面，分别绘制占用与运维分布并在每次显示时刷新。
 */
#pragma once

// 电桩状态页:分别展示占用状态和运维状态的数量与占比。

#include <QWidget>

#include "api/api_client.h"

class QLabel;
class QTableWidget;

/// @brief 并列展示占用和运维统计，每次显示都重新请求快照。
class ChargerStatusPage : public QWidget {
	Q_OBJECT
public:
	/** @brief 建立两个独立状态表并连接快照结果，加载由显示事件触发。
	 * @param api 非拥有的共享客户端，必须比本对象存活更久。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit ChargerStatusPage(ops::ApiClient *api, QWidget *parent = nullptr);

	/** @brief 每次调用都显示刷新提示并请求最新状态快照。
	 */
	void refresh();

protected:
	/** @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
	 * @param event Qt 传入的显示事件，不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/** @brief 填充状态、数量、百分比及进度条，进度条按 0..1 限幅。
	 * @param table 接收行数据并拥有单元格控件的表格。
	 * @param rows 按显示顺序排列的状态统计行。
	 */
	void populateTable(QTableWidget *table, const QList<ops::ChargerStatusCount> &rows);

	ops::ApiClient *m_api;						///< 非拥有的共享客户端，须比界面对象存活更久。
	QTableWidget *m_occupancyTable = nullptr;	///< 占用状态表；由 Qt 对象树管理。
	QTableWidget *m_operationalTable = nullptr; ///< 运维状态表；由 Qt 对象树管理。
	QLabel *m_totalLabel = nullptr;				///< 总数及加载提示标签；由 Qt 对象树管理。
};
