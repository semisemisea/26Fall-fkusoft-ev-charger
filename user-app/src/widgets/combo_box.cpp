/**
 * @file combo_box.cpp
 * @brief 定制下拉框绘制、选项委托及弹层圆角遮罩。
 */
#include "combo_box.h"

#include "common/theme.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionComboBox>
#include <QStyledItemDelegate>
#include <QTimer>

namespace {

	/**
	 * @brief 自绘下拉选项的圆角选中背景，并固定选项行高。
	 */
	class PopupDelegate : public QStyledItemDelegate {
	public:
		using QStyledItemDelegate::QStyledItemDelegate;

		// 自绘选项：选中/悬停画圆角底色，文字着色区分
		/**
		 * @brief 绘制下拉选项的高亮底色和左对齐文本。
		 * @param painter Qt 提供的画笔；调用内成对保存和恢复绘制状态。
		 * @param option 选项区域及选中、悬停状态。
		 * @param index 当前选项的数据索引，从 DisplayRole 读取文字。
		 */
		void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
			painter->save();
			painter->setRenderHint(QPainter::Antialiasing);
			const bool selected = option.state & QStyle::State_Selected;
			const bool hovered = option.state & QStyle::State_MouseOver;
			if (selected || hovered) {
				painter->setPen(Qt::NoPen);
				painter->setBrush(selected ? theme::primaryBg() : theme::fillHover());
				painter->drawRoundedRect(option.rect.adjusted(3, 1, -3, -1), 6, 6);
			}
			painter->setPen(selected ? theme::primaryDeep() : theme::textPrimary());
			painter->drawText(option.rect.adjusted(12, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
							  index.data(Qt::DisplayRole).toString());
			painter->restore();
		}

		// 固定选项行高 34
		/**
		 * @brief 保留基类建议宽度并固定行高为 34 像素。
		 * @param option 当前项样式选项。
		 * @param index 当前项数据索引。
		 * @return 修改高度后的选项建议尺寸。
		 */
		QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
			QSize size = QStyledItemDelegate::sizeHint(option, index);
			size.setHeight(34);
			return size;
		}
	};

} // namespace

/**
 * @details 使用自定义 QListView + PopupDelegate 作为弹层；选中项后立即关闭弹层
 */
ComboBox::ComboBox(QWidget *parent)
	: QComboBox(parent) {
	setMinimumHeight(38);
	auto *listView = new QListView(this);
	listView->setItemDelegate(new PopupDelegate(listView));
	listView->setObjectName(QStringLiteral("comboPopup"));
	setView(listView);
	connect(listView, &QAbstractItemView::activated, this, [this] { hidePopup(); });
	connect(listView, &QAbstractItemView::clicked, this, [this] {
		QTimer::singleShot(0, this, [this] { hidePopup(); });
	});
}

/**
 * @details 用多边形遮罩把弹层裁成 10px 圆角
 */
void ComboBox::showPopup() {
	QComboBox::showPopup();
	QWidget *popup = view() ? view()->window() : nullptr;
	if (popup) {
		QPainterPath path;
		path.addRoundedRect(QRectF(popup->rect()), 10, 10);
		popup->setMask(QRegion(path.toFillPolygon().toPolygon()));
	}
}

/**
 * @details 右侧预留 46px 箭头区，最小高度 38
 */
QSize ComboBox::sizeHint() const {
	QSize size = QComboBox::sizeHint();
	size.rwidth() += 46;
	size.setHeight(qMax(size.height(), 38));
	return size;
}

/**
 * @details 自绘圆角边框（聚焦/悬停时高亮）、省略文本与 chevron 箭头
 */
void ComboBox::paintEvent(QPaintEvent *event) {
	Q_UNUSED(event);

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	painter.setPen(theme::border());
	painter.setBrush(Qt::white);
	painter.drawRoundedRect(frame, 10, 10);

	const QRect textRect = rect().adjusted(12, 0, -34, 0);
	painter.setPen(theme::textPrimary());
	painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
					 painter.fontMetrics().elidedText(currentText(), Qt::ElideRight, textRect.width()));

	const QPointF center(width() - 18, height() / 2.0 + 1);
	QPainterPath chevron;
	chevron.moveTo(center.x() - 4.5, center.y() - 2.5);
	chevron.lineTo(center.x(), center.y() + 2.5);
	chevron.lineTo(center.x() + 4.5, center.y() - 2.5);
	painter.setPen(QPen(theme::textSecondary(), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(chevron);
}
