#pragma once

// 用户管理页:用户列表(ID/手机号/昵称/余额/注册时间/状态),
// 手机号模糊搜索;冻结/解冻操作(仅 ADMIN,风控场景)。

#include <QWidget>

#include "api/apiclient.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class UserPage : public QWidget {
	Q_OBJECT
public:
	explicit UserPage(ops::ApiClient *api, QWidget *parent = nullptr);

	void refresh();

protected:
	void showEvent(QShowEvent *event) override;

private:
	int selectedUserRow() const;
	void updateFrozenButton();
	void updatePager();

	ops::ApiClient *m_api;
	QLineEdit *m_searchEdit = nullptr;
	QTableWidget *m_table = nullptr;
	QPushButton *m_freezeButton = nullptr;
	QLabel *m_hintLabel = nullptr;
	QLabel *m_pageLabel = nullptr;
	QPushButton *m_prevButton = nullptr;
	QPushButton *m_nextButton = nullptr;
	QList<ops::AdminUserRow> m_rows;
	int m_page = 1;
	bool m_hasNext = false;
	bool m_loaded = false;
};
