/** @file
 * @brief 腾讯地图选点对话框，校验 iframe 来源和坐标标题，兼容缺少 WebEngine 或密钥的情况。
 */
#pragma once

#include <QDialog>
#include <QSize>

#include <optional>

/// @brief 腾讯地图选点对话框；通过受校验的页面标题接收坐标，缺少组件或密钥时显示提示。
class MapPickerDialog : public QDialog {
public:
	/// @brief 地图选点的纬度和经度，单位为度。
	struct Coordinate {
		double latitude = 0.0;	///< 纬度，单位度。
		double longitude = 0.0; ///< 经度，单位度。
	};

	/** @brief 校验初始坐标并建立地图选点器；无密钥或 WebEngine 时显示提示。
	 * @param mapKey 腾讯地图密钥，空白时显示未配置提示。
	 * @param initialLatitude 初始纬度；与经度一起校验，不合法时使用默认中心。
	 * @param initialLongitude 初始经度。
	 * @param parent Qt 父对象；负责子对象生命周期。
	 * @param windowSize 初始窗口尺寸，默认使用管理端的横向布局。
	 */
	MapPickerDialog(const QString &mapKey, double initialLatitude, double initialLongitude,
					QWidget *parent = nullptr, QSize windowSize = QSize(760, 560));

	/** @brief 返回当前保存的纬度。
	 * @return 当前坐标的纬度，单位度。
	 */
	[[nodiscard]] double latitude() const { return m_coordinate.latitude; }
	/** @brief 返回当前保存的经度。
	 * @return 当前坐标的经度，单位度。
	 */
	[[nodiscard]] double longitude() const { return m_coordinate.longitude; }

	/** @brief 解析专用前缀的纬经度标题；格式、数值或范围错误返回 nullopt。
	 * @param title 待解析的页面标题。
	 * @return 有效坐标；无效输入返回 std::nullopt。
	 */
	static std::optional<Coordinate> coordinateFromTitle(const QString &title);

private:
	Coordinate m_coordinate; ///< 已校验的当前坐标或回退初始坐标。
};
