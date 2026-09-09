#pragma once

#include "api/api_client.h"
#include "app/session.h"
#include "models/route_playback.h"
#include "models/station.h"

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QWebEngineView;

/// 自有路线地图与模拟导航页；起点沿用共享会话中的手动选点。
class NavigationView : public QWidget {
	Q_OBJECT
public:
	explicit NavigationView(Session &session, ApiClient &api, QWidget *parent = nullptr);
	void open(const Station &station);
signals:
	void backRequested();

protected:
	void hideEvent(QHideEvent *event) override;

private:
	void requestRoute();
	void pause();
	void updatePlayback();
	void showMap(const QJsonObject &route);
	Session &m_session;
	ApiClient &m_api;
	Station m_station;
	QString m_mode = QStringLiteral("driving");
	quint64 m_requestId = 0;
	RoutePlayback m_playback;
	QElapsedTimer m_elapsed;
	QTimer *m_timer = nullptr;
	QLabel *m_title = nullptr;
	QLabel *m_status = nullptr;
	QLabel *m_progress = nullptr;
	QListWidget *m_steps = nullptr;
	QPushButton *m_play = nullptr;
	QPushButton *m_restart = nullptr;
	QVBoxLayout *m_layout = nullptr;
	QWebEngineView *m_webView = nullptr;
	QJsonObject m_route;
	bool m_mapReady = false;
};
