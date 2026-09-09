/** @file
 * @brief 电站分页查询、选中站点的电桩管理，以及电站新增和地图选点表单。
 */
#pragma once

// 充电站管理页:管理电站，并在选中电站后管理其站内电桩。

#include <QDialog>
#include <QWidget>

#include "api/api_client.h"
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

/// @brief 电站列表与站内电桩管理页面；以电站 ID 过滤迟到的明细响应。
class StationPage : public QWidget {
	Q_OBJECT
public:
	/** @brief 建立电站与电桩两级表格，连接分页、选择、写操作和结果回调。
	 * @param api 非拥有的共享客户端，必须比本对象存活更久。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit StationPage(ops::ApiClient *api, QWidget *parent = nullptr);

	/** @brief 按当前筛选条件和页码重新请求数据。
	 */
	void refresh();

protected:
	/** @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
	 * @param event Qt 传入的显示事件，不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/** @brief 切换当前电站，清空旧明细并异步请求新站电桩。
	 * @param stationId 所属或目标电站 ID。
	 * @param stationName 当前电站的展示名称。
	 */
	void showStationChargers(qint64 stationId, const QString &stationName);
	/** @brief 按电桩值对象重绘明细，累计分钟转换为一位小数小时。
	 * @param chargers 待显示的站内电桩列表。
	 */
	void applyChargerRows(const QList<ops::Charger> &chargers);
	/** @brief 返回选中的电桩行号，无选择返回 -1。
	 * @return 选中行的零基索引，没有选择时为 -1。
	 */
	int selectedChargerRow() const;
	/** @brief 综合权限、当前选择和请求进行中标记启用按钮，重启另受状态限制。
	 */
	void updateChargerActions();
	/** @brief 刷新当前搜索页，列表回调按电站 ID 恢复选择并加载电桩。
	 */
	void reloadSelectedStation();
	/** @brief 依页码和下一页标志设置翻页条可见性及按钮状态。
	 */
	void updatePager();

	ops::ApiClient *m_api;						   ///< 非拥有的共享客户端，须比界面对象存活更久。
	QLineEdit *m_searchEdit = nullptr;			   ///< 搜索输入框；由 Qt 对象树管理。
	QTableWidget *m_table = nullptr;			   ///< 主列表表格；由 Qt 对象树管理。
	QPushButton *m_addButton = nullptr;			   ///< 新增电站按钮；由 Qt 对象树管理。
	QLabel *m_stationHintLabel = nullptr;		   ///< 电站列表提示标签；由 Qt 对象树管理。
	QLabel *m_pageLabel = nullptr;				   ///< 页码标签；由 Qt 对象树管理。
	QPushButton *m_prevButton = nullptr;		   ///< 上一页按钮；由 Qt 对象树管理。
	QPushButton *m_nextButton = nullptr;		   ///< 下一页按钮；由 Qt 对象树管理。
	QLabel *m_chargerHeading = nullptr;			   ///< 站内电桩标题；由 Qt 对象树管理。
	QTableWidget *m_chargerTable = nullptr;		   ///< 站内电桩明细表；由 Qt 对象树管理。
	QPushButton *m_addChargerButton = nullptr;	   ///< 新增电桩按钮；由 Qt 对象树管理。
	QPushButton *m_editChargerButton = nullptr;	   ///< 编辑电桩按钮；由 Qt 对象树管理。
	QPushButton *m_deleteChargerButton = nullptr;  ///< 删除电桩按钮；由 Qt 对象树管理。
	QPushButton *m_restartChargerButton = nullptr; ///< 远程重启按钮；由 Qt 对象树管理。
	QLabel *m_chargerHintLabel = nullptr;		   ///< 电桩操作与加载提示标签；由 Qt 对象树管理。
	QList<ops::StationSummary> m_rows;			   ///< 与主表行序对应的值对象缓存。
	QList<ops::Charger> m_chargerRows;			   ///< 与站内电桩表行序对应的值对象缓存。
	qint64 m_currentStationId = -1;				   ///< 当前选中电站 ID；-1 表示未选择。
	QString m_currentStationName;				   ///< 当前选中电站的展示名称。
	int m_page = 1;								   ///< 当前请求页码，从 1 开始。
	bool m_hasNext = false;						   ///< 最近响应元数据允许继续翻页的标记。

	bool m_chargerMutationPending = false; ///< 电桩写操作或重启尚未完成时为 true，防止按钮重复提交。
};

// 新增电站对话框；创建后可在当前页面逐个添加电桩。
/// @brief 采集站名、经纬度和单价，并在接受前校验输入。
class AddStationDialog : public QDialog {
	Q_OBJECT
public:
	/** @brief 建立电站表单，连接地图选点及接受前的名称、坐标和价格校验。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit AddStationDialog(QWidget *parent = nullptr);

	/** @brief 将当前控件值复制为提交表单，不执行网络操作。
	 * @return 当前表单值副本。
	 */
	ops::StationForm form() const;

private:
	QLineEdit *m_nameEdit = nullptr;			 ///< 站名输入框；由 Qt 对象树管理。
	QPushButton *m_pickLocationButton = nullptr; ///< 地图选点入口按钮；由 Qt 对象树管理。
	QLineEdit *m_latEdit = nullptr;				 ///< 纬度输入框；由 Qt 对象树管理。
	QLineEdit *m_lonEdit = nullptr;				 ///< 经度输入框；由 Qt 对象树管理。
	QLineEdit *m_priceEdit = nullptr;			 ///< 以元每度输入的单价框；由 Qt 对象树管理。
};
