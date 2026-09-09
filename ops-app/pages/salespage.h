/** @file
 * @brief 营收指标卡片与 7/30 日趋势，包含 Qt Charts 绘图和无图表组件时的文本汇总。
 */
#pragma once

// 销售业绩页:三大营收指标卡片 + 近7日/30日营收趋势折线图(QtCharts)。

#include <QWidget>

#ifdef OPS_APP_HAS_CHARTS
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#endif

#include "api/apiclient.h"

class QLabel;
#ifdef OPS_APP_HAS_CHARTS
class QLineSeries;
class QChart;
#endif

/// @brief 营收卡片与趋势页面；可选 Charts 缺失时使用文本汇总。
class SalesPage : public QWidget {
	Q_OBJECT
public:
	/** @brief 建立营收卡片和可选图表，并连接范围切换和异步指标结果。
	 * @param api 非拥有的共享客户端，必须比本对象存活更久。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit SalesPage(ops::ApiClient *api, QWidget *parent = nullptr);

	// 切到本页时刷新
	/** @brief 仅在 m_loaded 为 false 时发起首次加载；标记在发送请求前置为 true。
	 */
	void refresh();

private:
	/** @brief 创建初始含标题与占位符的营收卡片。
	 * @param title 卡片初始展示标题。
	 * @param accent 卡片顶边强调色（CSS 颜色文本）。
	 * @return 当前页面拥有的 QLabel，后续响应更新其文本。
	 */
	QLabel *makeCard(const QString &title, const QString &accent);
	/** @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
	 * @param event Qt 传入的显示事件，不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

	ops::ApiClient *m_api; ///< 非拥有的共享客户端，须比界面对象存活更久。

	QLabel *m_todayCard = nullptr;	///< 今日营收卡片；由 Qt 对象树管理。
	QLabel *m_monthCard = nullptr;	///< 本月营收卡片；由 Qt 对象树管理。
	QLabel *m_totalCard = nullptr;	///< 累计营收卡片；由 Qt 对象树管理。
	QLabel *m_extraLabel = nullptr; ///< 资源统计及错误提示标签；由 Qt 对象树管理。

#ifdef OPS_APP_HAS_CHARTS
	QChart *m_chart = nullptr;		  ///< 由图表视图拥有的趋势图；由 Qt 对象树管理。
	QLineSeries *m_series = nullptr;  ///< 由图表接管的营收序列；由 Qt 对象树管理。
	QDateTimeAxis *m_axisX = nullptr; ///< 图表拥有的日期横轴；由 Qt 对象树管理。
	QValueAxis *m_axisY = nullptr;	  ///< 图表拥有的金额纵轴；由 Qt 对象树管理。
#else
	QLabel *m_chartFallback = nullptr; ///< 无 Charts 时的文本汇总标签；由 Qt 对象树管理。
#endif
	QString m_range = QStringLiteral("7d"); ///< 当前趋势时间范围 7d 或 30d。
	bool m_loaded = false;					///< 已触发首次加载的标记；并不表示请求一定成功。
};
