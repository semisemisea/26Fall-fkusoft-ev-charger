/** @file
 * @brief 电桩新增与编辑表单，管理电站归属锁定、类型、功率及运维状态输入。
 */
#pragma once

#include <QDialog>

#include "api/api_client.h"

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;

/// @brief 新增或编辑电桩的表单；编辑时锁定所属电站。
class ChargerDialog : public QDialog {
	Q_OBJECT
public:
	/** @brief 建立电桩表单；有初始电桩时回填字段并锁定电站，控件由对话框拥有。
	 * @param charger 可选初始电桩；空指针表示新增，构造时复制控件所需值。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	explicit ChargerDialog(const ops::Charger *charger = nullptr, QWidget *parent = nullptr);
	/** @brief 建立电桩表单；有初始电桩时回填字段并锁定电站，控件由对话框拥有。
	 * @param stationId 所属或目标电站 ID。
	 * @param charger 可选初始电桩；空指针表示新增，构造时复制控件所需值。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 */
	ChargerDialog(qint64 stationId, const ops::Charger *charger, QWidget *parent = nullptr);

	/** @brief 返回表单中的所属电站 ID。
	 * @return 输入的电站 ID；未校验文本转换失败时为零。
	 */
	qint64 stationId() const;
	/** @brief 将当前控件值复制为提交表单，不执行网络操作。
	 * @return 当前表单值副本。
	 */
	ops::ChargerForm form() const;

private:
	QLineEdit *m_stationIdEdit = nullptr;  ///< 所属电站 ID 输入框；由 Qt 对象树管理。
	QComboBox *m_typeBox = nullptr;		   ///< 充电类型选项；由 Qt 对象树管理。
	QDoubleSpinBox *m_powerSpin = nullptr; ///< 千瓦功率输入框；由 Qt 对象树管理。
	QComboBox *m_operationalBox = nullptr; ///< 运维状态选项；由 Qt 对象树管理。
};
