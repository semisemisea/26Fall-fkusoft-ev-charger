/**
 * @file ChargePollThread.h
 * @brief 在独立线程中轮询充电订单，并通过队列信号向界面交付计量快照。
 */
#pragma once

#include <QJsonObject>
#include <QThread>
#include <atomic>

/**
 * @brief 充电订单轮询线程：每 5 秒拉取 GET /orders/{id}，meterUpdated 信号跨线程回主线程刷新界面
 * @details configure() 仅在线程未运行时调用；销毁前必须 requestStop() 并等待退出。
 */
class ChargePollThread : public QThread {
	Q_OBJECT

public:
	/**
	 * @brief 创建尚未运行的轮询线程，并注册 QJsonObject 跨线程信号类型。
	 * @param parent Qt 父对象；非空时由父对象管理所创建对象的生命周期。
	 */
	explicit ChargePollThread(QObject *parent = nullptr);

	/**
	 * @brief 配置轮询目标：订单 ID、服务地址与 Bearer 令牌；启动线程前调用
	 * @param orderId 轮询目标订单的服务端标识。
	 * @param baseUrl 包含 API 前缀的服务根地址，例如 http://localhost:8080/api/v1。
	 * @param token Bearer 访问令牌；空字符串表示不携带登录凭据。
	 * @pre 线程尚未启动或上一次运行已经结束。
	 */
	void configure(int orderId, const QString &baseUrl, const QString &token);
	/**
	 * @brief 请求停止并阻塞等待线程退出
	 * @note 正在进行的网络请求不会被立即中止，等待可能持续到请求完成或超时。
	 */
	void requestStop();

signals:
	/**
	 * @brief 充电中订单的最新数据（跨线程队列投递到主线程）
	 * @param order 服务端返回的订单快照，金额与电量直接用于展示。
	 */
	void meterUpdated(const QJsonObject &order);

protected:
	/**
	 * @brief 线程主体：循环拉取订单并按间隔休眠
	 */
	void run() override;

private:
	int m_orderId = 0;				 ///< 启动前配置的目标订单标识。
	QString m_baseUrl;				 ///< 包含 API 前缀的服务根地址。
	QString m_token;				 ///< 启动前复制的访问令牌，避免线程读取可变会话。
	std::atomic<bool> m_stop{false}; ///< 跨线程可见的停止请求标志；不保护其他配置字段。
};
