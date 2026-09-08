/** @file
 * @brief 用户手机号查询、分页列表及权限控制下的冻结和解冻操作。
 */
#pragma once

// 用户管理页:用户列表(ID/手机号/昵称/余额/注册时间/状态),
// 手机号模糊搜索;冻结/解冻操作(仅 ADMIN,风控场景)。

#include <QWidget>

#include "api/api_client.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

/// @brief 带手机号查询、分页及冻结操作的用户管理页面。
class UserPage : public QWidget {
	Q_OBJECT
public:
	/** @brief 建立手机号查询、用户列表和分页控件，连接冻结操作及结果回调。
	 * @param api 非拥有的共享客户端，必须比本对象存活更久。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit UserPage(ops::ApiClient *api, QWidget *parent = nullptr);

	/** @brief 仅在 m_loaded 为 false 时发起首次加载；标记在发送请求前置为 true。
	 */
	void refresh();

protected:
	/** @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
	 * @param event Qt 传入的显示事件，不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/** @brief 返回选中的用户行号，无选择返回 -1。
	 * @return 选中行的零基索引，没有选择时为 -1。
	 */
	int selectedUserRow() const;
	/** @brief 依据选择用户的冻结状态设置按钮文案，并按管理员权限启用。
	 */
	void updateFrozenButton();
	/** @brief 依页码和下一页标志设置翻页条可见性及按钮状态。
	 */
	void updatePager();

	ops::ApiClient *m_api;				   ///< 非拥有的共享客户端，须比界面对象存活更久。
	QLineEdit *m_searchEdit = nullptr;	   ///< 搜索输入框；由 Qt 对象树管理。
	QTableWidget *m_table = nullptr;	   ///< 主列表表格；由 Qt 对象树管理。
	QPushButton *m_freezeButton = nullptr; ///< 冻结或解冻按钮；由 Qt 对象树管理。
	QLabel *m_hintLabel = nullptr;		   ///< 用户数量提示标签；由 Qt 对象树管理。
	QLabel *m_pageLabel = nullptr;		   ///< 页码标签；由 Qt 对象树管理。
	QPushButton *m_prevButton = nullptr;   ///< 上一页按钮；由 Qt 对象树管理。
	QPushButton *m_nextButton = nullptr;   ///< 下一页按钮；由 Qt 对象树管理。
	QList<ops::AdminUserRow> m_rows;	   ///< 与主表行序对应的值对象缓存。
	int m_page = 1;						   ///< 当前请求页码，从 1 开始。
	bool m_hasNext = false;				   ///< 最近响应元数据允许继续翻页的标记。
	bool m_loaded = false;				   ///< 已触发首次加载的标记；并不表示请求一定成功。
};
