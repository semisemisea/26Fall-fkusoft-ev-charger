/**
 * @file Toast.cpp
 * @brief 在锚点窗口顶部显示非阻塞提示，并自动完成入场与退出销毁。
 */
#include "Toast.h"

#include "common/Demo.h"
#include "common/Theme.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

namespace {

	// 在 anchor 所在窗口顶部居中弹出提示：替换旧提示，淡入下滑入场，3s 后淡出销毁
	/**
	 * @brief 在锚点所属窗口顶部显示一条可自动销毁的轻提示。
	 * @param anchor 用于定位顶层窗口的控件，为空时直接返回。
	 * @param accent 提示圆点颜色，区分成功、失败和一般消息。
	 * @param text 提示文本。
	 * @details 旧提示延迟删除，新提示由宿主窗口拥有；动画和定时回调均以提示控件为上下文。
	 */
	void showToast(QWidget *anchor, const QColor &accent, const QString &text) {
		QWidget *host = anchor ? anchor->window() : nullptr;
		if (!host) {
			return;
		}
		if (auto *existing = host->findChild<QWidget *>(QStringLiteral("toastWrapper"))) {
			existing->deleteLater();
		}

		auto *wrapper = new QWidget(host);
		wrapper->setObjectName(QStringLiteral("toastWrapper"));
		wrapper->setAttribute(Qt::WA_TransparentForMouseEvents);

		auto *card = new QFrame(wrapper);
		card->setObjectName(QStringLiteral("toast"));

		auto *dot = new QLabel(card);
		dot->setObjectName(QStringLiteral("toastDot"));
		// 状态点颜色随 success/error/info 参数变化，属于运行时动态样式，保留内联
		dot->setStyleSheet(QStringLiteral("background: %1;").arg(accent.name()));

		auto *label = new QLabel(text, card);
		label->setObjectName(QStringLiteral("toastText"));

		auto *row = new QHBoxLayout(card);
		row->setContentsMargins(14, 9, 16, 9);
		row->setSpacing(8);
		row->addWidget(dot);
		row->addWidget(label);

		auto *shadow = new QGraphicsDropShadowEffect(card);
		shadow->setBlurRadius(26);
		shadow->setColor(theme::shadowInk());
		shadow->setOffset(0, 4);
		card->setGraphicsEffect(shadow);

		auto *wrapperLayout = new QHBoxLayout(wrapper);
		wrapperLayout->setContentsMargins(0, 0, 0, 0);
		wrapperLayout->addWidget(card);

		wrapper->adjustSize();
		const int x = (host->width() - wrapper->width()) / 2;
		const int targetY = 44;
		wrapper->move(x, targetY - 10);

		auto *opacity = new QGraphicsOpacityEffect(wrapper);
		opacity->setOpacity(0.0);
		wrapper->setGraphicsEffect(opacity);

		auto *fade = new QPropertyAnimation(opacity, QByteArrayLiteral("opacity"), wrapper);
		fade->setDuration(demo::ms(160));
		fade->setStartValue(0.0);
		fade->setEndValue(1.0);
		fade->setEasingCurve(QEasingCurve::OutCubic);
		fade->start(QAbstractAnimation::DeleteWhenStopped);

		auto *slide = new QPropertyAnimation(wrapper, QByteArrayLiteral("pos"), wrapper);
		slide->setDuration(demo::ms(200));
		slide->setStartValue(QPoint(x, targetY - 10));
		slide->setEndValue(QPoint(x, targetY));
		slide->setEasingCurve(QEasingCurve::OutCubic);
		slide->start(QAbstractAnimation::DeleteWhenStopped);

		QTimer::singleShot(demo::ms(3000), wrapper, [wrapper, opacity] {
			auto *out = new QPropertyAnimation(opacity, QByteArrayLiteral("opacity"), wrapper);
			out->setDuration(demo::ms(220));
			out->setStartValue(1.0);
			out->setEndValue(0.0);
			out->setEasingCurve(QEasingCurve::InCubic);
			QObject::connect(out, &QAbstractAnimation::finished, wrapper, &QWidget::deleteLater);
			out->start(QAbstractAnimation::DeleteWhenStopped);
		});

		wrapper->show();
		wrapper->raise();
	}

} // namespace

namespace Toast {

	// 三种级别仅提示点颜色不同
	/**
	 * @brief 成功状态，使用绿色提示点。
	 */
	void success(QWidget *anchor, const QString &text) {
		showToast(anchor, theme::success(), text);
	}

	/**
	 * @brief 失败状态，使用红色提示点。
	 */
	void error(QWidget *anchor, const QString &text) {
		showToast(anchor, theme::error(), text);
	}

	/**
	 * @brief 一般信息，使用品牌主色提示点。
	 */
	void info(QWidget *anchor, const QString &text) {
		showToast(anchor, theme::primary(), text);
	}
} // namespace Toast
