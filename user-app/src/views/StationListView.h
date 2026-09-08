#pragma once

#include "api/ApiClient.h"
#include "app/Session.h"
#include "models/Station.h"

#include <QVector>
#include <QWidget>

class ComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class Spinner;
class QVBoxLayout;
class StationCard;

// 找桩页：附近电站列表（GET /stations/nearby，按距离排序）、定位切换、搜索与 AI 推荐横幅
class StationListView : public QWidget {
	Q_OBJECT

public:
	// 构造函数：搭建定位/搜索/列表/推荐横幅界面
	explicit StationListView(Session &session, ApiClient &api, QWidget *parent = nullptr);

signals:
	// 用户点击某电站卡片（含 AI 推荐横幅），请求打开详情页
	void stationSelected(const Station &station);
	// 用户请求导航到某电站，由 MainWindow 打开导航页
	void navigateRequested(const Station &station);

protected:
	// 页面每次显示时刷新附近电站列表
	void showEvent(QShowEvent *event) override;

private:
	// 重新加载附近电站（GET /stations/nearby）并重建卡片列表
	void reload();
	// 按搜索框关键字过滤卡片可见性
	void applyFilter();
	// 按当前空闲率生成本地推荐
	void loadRecommendation();

	Session &m_session;
	ApiClient &m_api;
	ComboBox *m_locationCombo = nullptr;
	QLineEdit *m_searchEdit = nullptr;
	QPushButton *m_bannerButton = nullptr;
	QLabel *m_statusLabel = nullptr;
	Spinner *m_spinner = nullptr;
	QScrollArea *m_scrollArea = nullptr;
	QVBoxLayout *m_cardsLayout = nullptr;
	QVector<StationCard *> m_cards;
	Station m_recommendedStation;
	bool m_hasRecommendation = false;
	bool m_listAnimated = false;
};
