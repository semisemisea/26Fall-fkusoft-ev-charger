/**
 * @file info_pages.h
 * @brief 实现历史订单、钱包流水、预约记录与关于系统页面。
 */
#pragma once

#include "api/api_client.h"

#include <QWidget>

class QLabel;
class Spinner;
class QVBoxLayout;

// 历史与信息页合集：订单/流水/预约记录 + 车辆/关于，结构相同——进入时 load() 拉取列表

/**
 * @brief 历史订单（GET /orders）
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class OrderHistoryView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：注入 API 客户端并搭建带返回键的列表页骨架
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit OrderHistoryView(ApiClient &api, QWidget *parent = nullptr);

signals:
	/**
	 * @brief 用户点击返回
	 */
	void backRequested();

protected:
	/**
	 * @brief 页面显示时触发列表加载
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/**
	 * @brief 拉取历史订单列表并渲染卡片
	 */
	void load();

	ApiClient &m_api;					 ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLabel *m_statusLabel = nullptr;	 ///< 列表加载、空数据或错误状态标签。
	Spinner *m_spinner = nullptr;		 ///< 由页面拥有的加载动画控件。
	QVBoxLayout *m_listLayout = nullptr; ///< 保留状态行与底部弹性空间的历史卡片列表布局。
};

/**
 * @brief 钱包流水（GET /me/wallet/transactions）
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class TransactionsView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：注入 API 客户端并搭建流水列表页骨架
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit TransactionsView(ApiClient &api, QWidget *parent = nullptr);

signals:
	/**
	 * @brief 用户点击返回
	 */
	void backRequested();

protected:
	/**
	 * @brief 页面显示时触发流水加载
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/**
	 * @brief 拉取钱包流水并渲染卡片（充值 / 扣款 / 退款 / 调整）
	 */
	void load();

	ApiClient &m_api;					 ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLabel *m_statusLabel = nullptr;	 ///< 列表加载、空数据或错误状态标签。
	Spinner *m_spinner = nullptr;		 ///< 由页面拥有的加载动画控件。
	QVBoxLayout *m_listLayout = nullptr; ///< 保留状态行与底部弹性空间的历史卡片列表布局。
};

/**
 * @brief 预约记录（GET /reservations）
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class ReservationHistoryView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：注入 API 客户端并搭建预约记录列表页骨架
	 * @param api 共享网络客户端的非拥有引用，必须比当前对象存活更久。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ReservationHistoryView(ApiClient &api, QWidget *parent = nullptr);

signals:
	/**
	 * @brief 用户点击返回
	 */
	void backRequested();

protected:
	/**
	 * @brief 页面显示时触发预约记录加载
	 * @param event Qt 派发的事件，调用期间有效且不转移所有权。
	 */
	void showEvent(QShowEvent *event) override;

private:
	/**
	 * @brief 拉取预约记录并渲染卡片
	 */
	void load();

	ApiClient &m_api;					 ///< 共享网络出口；页面不拥有客户端，主窗口通过 Qt 父子关系拥有它。
	QLabel *m_statusLabel = nullptr;	 ///< 列表加载、空数据或错误状态标签。
	Spinner *m_spinner = nullptr;		 ///< 由页面拥有的加载动画控件。
	QVBoxLayout *m_listLayout = nullptr; ///< 保留状态行与底部弹性空间的历史卡片列表布局。
};

/**
 * @brief 关于系统页（版本与简介）
 * @details 控件及布局通过 Qt 父子树管理；注入的会话和网络客户端不转移所有权。
 */
class AboutView : public QWidget {
	Q_OBJECT

public:
	/**
	 * @brief 构造：搭建关于页
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit AboutView(QWidget *parent = nullptr);

signals:
	/**
	 * @brief 用户点击返回
	 */
	void backRequested();
};
