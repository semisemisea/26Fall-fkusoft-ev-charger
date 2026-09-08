/** @file
 * @brief 用户手机号查询、分页列表及权限控制下的冻结和解冻操作。
 */
#include "userpage.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

	/// @brief 表格列索引；与表头和填充位置保持一致。
	enum Col { ColId = 0,
			   ColPhone,
			   ColNickname,
			   ColBalance,
			   ColCreatedAt,
			   ColStatus };

} // namespace

/// @brief 建立手机号查询、用户列表和分页控件，连接冻结操作及结果回调。
UserPage::UserPage(ops::ApiClient *api, QWidget *parent)
	: QWidget(parent), m_api(api) {
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(24, 24, 24, 24);
	root->setSpacing(16);

	auto *topBar = new QHBoxLayout;
	auto *title = new QLabel(tr("用户管理"), this);
	title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
	topBar->addWidget(title);
	topBar->addStretch();
	topBar->addWidget(new QLabel(tr("手机号:"), this));
	m_searchEdit = new QLineEdit(this);
	m_searchEdit->setPlaceholderText(tr("模糊搜索, 如 138"));
	m_searchEdit->setClearButtonEnabled(true);
	m_searchEdit->setFixedWidth(220);
	topBar->addWidget(m_searchEdit);
	m_freezeButton = new QPushButton(tr("冻结/解冻"), this);
	m_freezeButton->setObjectName(QStringLiteral("danger"));
	topBar->addWidget(m_freezeButton);
	root->addLayout(topBar);

	m_table = new QTableWidget(this);
	m_table->setColumnCount(6);
	m_table->setHorizontalHeaderLabels({tr("ID"), tr("手机号"), tr("昵称"),
										tr("余额 (元)"), tr("注册时间"), tr("状态")});
	m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setAlternatingRowColors(true);
	m_table->verticalHeader()->setVisible(false);
	m_table->verticalHeader()->setDefaultSectionSize(36);
	root->addWidget(m_table, 1);

	m_hintLabel = new QLabel(this);
	m_hintLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	root->addWidget(m_hintLabel);

	// 分页条:上一页/下一页/页码;服务端未返回分页 meta 时整行隐藏
	auto *pagerRow = new QHBoxLayout;
	pagerRow->addStretch();
	m_prevButton = new QPushButton(tr("上一页"), this);
	m_pageLabel = new QLabel(this);
	m_pageLabel->setStyleSheet(QStringLiteral("color: #8a8f98;"));
	m_nextButton = new QPushButton(tr("下一页"), this);
	pagerRow->addWidget(m_prevButton);
	pagerRow->addWidget(m_pageLabel);
	pagerRow->addWidget(m_nextButton);
	root->addLayout(pagerRow);
	m_prevButton->setEnabled(false);
	m_nextButton->setEnabled(false);
	m_pageLabel->setText(tr("第 1 页"));

	connect(m_prevButton, &QPushButton::clicked, this, [this] {
		if (m_page > 1) {
			--m_page;
			m_api->fetchUsers(m_searchEdit->text().trimmed(), m_page);
		}
	});
	connect(m_nextButton, &QPushButton::clicked, this, [this] {
		if (m_hasNext) {
			++m_page;
			m_api->fetchUsers(m_searchEdit->text().trimmed(), m_page);
		}
	});

	connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
		m_page = 1; // 新搜索回到第 1 页
		m_api->fetchUsers(m_searchEdit->text().trimmed(), m_page);
	});

	connect(m_api, &ops::ApiClient::usersFetched, this,
			[this](const QList<ops::AdminUserRow> &users, const ops::PageMeta &meta,
				   const QString &errorCode) {
				if (!errorCode.isEmpty()) {
					QMessageBox::warning(this, tr("加载失败"),
										 tr("用户列表加载失败(%1),请稍后重试").arg(errorCode));
					return;
				}
				m_rows = users;
				m_table->setRowCount(users.size());
				for (int i = 0; i < users.size(); ++i) {
					const auto &u = users.at(i);
					m_table->setItem(i, ColId,
									 new QTableWidgetItem(QString::number(u.id)));
					m_table->setItem(i, ColPhone, new QTableWidgetItem(u.phone));
					m_table->setItem(i, ColNickname, new QTableWidgetItem(u.nickname));
					m_table->setItem(i, ColBalance,
									 new QTableWidgetItem(ops::fenCents(u.walletBalanceFen)));
					m_table->setItem(i, ColCreatedAt,
									 new QTableWidgetItem(u.createdAt.left(10)));
					m_table->setItem(i, ColStatus,
									 new QTableWidgetItem(ops::statusText(u.status)));
				}
				m_hintLabel->setText(tr("共 %1 名用户。").arg(users.size()));
				m_hasNext = meta.valid && meta.hasNext;
				updatePager();
				updateFrozenButton();
			});

	connect(m_table, &QTableWidget::itemSelectionChanged, this,
			[this] { updateFrozenButton(); });

	connect(m_freezeButton, &QPushButton::clicked, this, [this] {
		const int row = selectedUserRow();
		if (row < 0 || row >= m_rows.size())
			return;
		const ops::AdminUserRow &u = m_rows.at(row);
		const bool toFrozen = u.status != QLatin1String("frozen");
		if (!m_api->canWrite()) {
			QMessageBox::warning(this, tr("无权限"), tr("只读管理员无法变更用户状态"));
			return;
		}
		const auto confirm = QMessageBox::question(
			this, tr("确认操作"),
			toFrozen ? tr("确认冻结用户 %1 (%2)?").arg(u.nickname, u.phone)
					 : tr("确认解冻用户 %1 (%2)?").arg(u.nickname, u.phone));
		if (confirm != QMessageBox::Yes)
			return;
		m_freezeButton->setEnabled(false);
		m_api->setUserStatus(u.id, toFrozen);
	});

	connect(m_api, &ops::ApiClient::userStatusChanged, this,
			[this](bool ok, const QString &errorCode) {
				m_freezeButton->setEnabled(true);
				if (ok) {
					QMessageBox::information(this, tr("操作成功"),
											 tr("用户状态已更新"));
					m_api->fetchUsers(m_searchEdit->text().trimmed(), m_page); // 留在当前页刷新列表
				} else {
					QMessageBox::warning(this, tr("操作失败"),
										 tr("错误码: %1").arg(errorCode));
				}
			});

	m_freezeButton->setEnabled(false); // 选中用户后才可用
}

/// @brief 依据选择用户的冻结状态设置按钮文案，并按管理员权限启用。
void UserPage::updateFrozenButton() {
	const int row = selectedUserRow();
	if (row < 0 || row >= m_rows.size()) {
		m_freezeButton->setEnabled(false);
		return;
	}
	const auto &u = m_rows.at(row);
	m_freezeButton->setText(u.status == QLatin1String("frozen") ? tr("解冻")
																: tr("冻结"));
	m_freezeButton->setEnabled(m_api->canWrite());
}

/// @brief 依页码和下一页标志设置翻页条可见性及按钮状态。
void UserPage::updatePager() {
	const bool show = m_hasNext || m_page > 1;
	m_prevButton->setVisible(show);
	m_nextButton->setVisible(show);
	m_pageLabel->setVisible(show);
	m_pageLabel->setText(tr("第 %1 页").arg(m_page));
	m_prevButton->setEnabled(m_page > 1);
	m_nextButton->setEnabled(m_hasNext);
}

/// @brief 返回选中的用户行号，无选择返回 -1。
int UserPage::selectedUserRow() const {
	const auto indexes = m_table->selectionModel()->selectedRows();
	return indexes.isEmpty() ? -1 : indexes.first().row();
}

/// @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
void UserPage::showEvent(QShowEvent *event) {
	/// @brief 先交给 QWidget 处理显示事件，再触发本页刷新。
	QWidget::showEvent(event);
	refresh();
}

/// @brief 按页面加载策略发起数据请求，结果由已连接的信号更新控件。
void UserPage::refresh() {
	if (m_loaded)
		return;
	m_loaded = true;
	m_api->fetchUsers({}, m_page);
}
