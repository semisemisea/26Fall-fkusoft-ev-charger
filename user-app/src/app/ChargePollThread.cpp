/**
 * @file ChargePollThread.cpp
 * @brief 在独立线程中轮询充电订单，并通过队列信号向界面交付计量快照。
 */
#include "ChargePollThread.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

namespace {
	/// @brief 每次请求完成后的轮询休眠间隔，单位毫秒。
	constexpr int kPollIntervalMs = 5000;
	/// @brief 单次订单请求的传输超时，限制停止等待的网络阶段。
	constexpr int kRequestTimeoutMs = 10000;
} // namespace

/**
 * @details 注册 QJsonObject 元类型，使信号可跨线程队列投递
 */
ChargePollThread::ChargePollThread(QObject *parent)
	: QThread(parent) {
	qRegisterMetaType<QJsonObject>();
}

/**
 * @details 保存轮询参数并复位停止标志（线程可重复配置使用）
 */
void ChargePollThread::configure(int orderId, const QString &baseUrl, const QString &token) {
	m_orderId = orderId;
	m_baseUrl = baseUrl;
	m_token = token;
	m_stop.store(false);
}

/**
 * @details 置位停止标志并等待线程结束，保证析构前线程已退出
 */
void ChargePollThread::requestStop() {
	m_stop.store(true);
	// run() 使用局部网络事件循环；quit() 不会中断它，仍等待响应完成或传输超时。
	quit();
	wait();
}

/**
 * @details 线程主体：同步阻塞式 GET /orders/{id}，charging 状态时发出 meterUpdated
 */
void ChargePollThread::run() {
	// 网络对象在线程内部创建，避免跨线程使用主线程的 QNetworkAccessManager
	QNetworkAccessManager manager;

	while (!m_stop.load()) {
		QNetworkRequest request(QUrl(QStringLiteral("%1/orders/%2").arg(m_baseUrl).arg(m_orderId)));
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_token).toUtf8());
		request.setRawHeader("X-Request-Id", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
		request.setTransferTimeout(kRequestTimeoutMs);

		QEventLoop loop;
		QNetworkReply *reply = manager.get(request);
		connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		loop.exec();

		// 仅在订单仍 charging 时刷新界面；结算后停止推送
		if (!m_stop.load() && reply->error() == QNetworkReply::NoError) {
			const QJsonObject envelope = QJsonDocument::fromJson(reply->readAll()).object();
			const QJsonObject order = envelope.value(QLatin1String("data")).toObject();
			if (order.value(QLatin1String("status")).toString() == QLatin1String("charging")) {
				emit meterUpdated(order);
			}
		}
		reply->deleteLater();

		// 5 秒间隔拆成 250ms 小步，缩短休眠阶段响应停止请求的延迟（网络等待仍受超时限制）
		for (int waited = 0; waited < kPollIntervalMs && !m_stop.load(); waited += 250) {
			msleep(250);
		}
	}
}
