#pragma once

#include <QApplication>
#include <QEvent>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <limits>

namespace evcharger {
	// 两个前端均读取可执行文件旁的配置；无效配置回退到三秒。
	inline int refreshIntervalMs() {
		QSettings settings(QCoreApplication::applicationDirPath() + QStringLiteral("/refresh.ini"), QSettings::IniFormat);
		bool ok = false;
		const int seconds = settings.value(QStringLiteral("refresh/intervalSeconds"), 3).toInt(&ok);
		return ok && seconds > 0 && seconds <= std::numeric_limits<int>::max() / 1000 ? seconds * 1000 : 3000;
	}

	// 由页面拥有；显示时刷新，隐藏时停止，模态编辑期间暂停定时请求。
	class PageRefresh final : public QObject {
	public:
		PageRefresh(QWidget *page, std::function<void()> refresh, bool periodic = true)
			: QObject(page), m_page(page), m_refresh(std::move(refresh)), m_periodic(periodic) {
			m_timer.setInterval(refreshIntervalMs());
			connect(&m_timer, &QTimer::timeout, this, [this] {
				if (m_page->isVisible() && !QApplication::activeModalWidget())
					m_refresh();
			});
			page->installEventFilter(this);
		}

	protected:
		bool eventFilter(QObject *object, QEvent *event) override {
			if (event->type() == QEvent::Show) {
				m_refresh();
				if (m_periodic)
					m_timer.start();
			} else if (event->type() == QEvent::Hide) {
				m_timer.stop();
			}
			return QObject::eventFilter(object, event);
		}

	private:
		QWidget *m_page;
		std::function<void()> m_refresh;
		bool m_periodic;
		QTimer m_timer;
	};
} // namespace evcharger
