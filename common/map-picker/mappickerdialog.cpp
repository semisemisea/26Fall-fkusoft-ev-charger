/** @file
 * @brief 腾讯地图选点对话框，校验 iframe 来源和坐标标题，兼容缺少 WebEngine 或密钥的情况。
 */
#include "evcharger/mappickerdialog.h"
#include <evcharger/logging.h>

#include <QDialogButtonBox>
#include <QLabel>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#ifdef EVCHARGER_HAS_WEBENGINE
#include <QWebEngineView>
#endif

Q_LOGGING_CATEGORY(mapPickerLog, "evcharger.ui.map", QtInfoMsg)

namespace {

	/// @brief 无效初始坐标时使用的大连默认纬度。
	constexpr double kDefaultLatitude = 38.914;
	/// @brief 无效初始坐标时使用的大连默认经度。
	constexpr double kDefaultLongitude = 121.614;
	/// @brief 页面标题向 C++ 传递选点结果的专用前缀。
	const QLatin1String kSelectionPrefix{"ev-charger-location:"};

	/** @brief 校验经纬度为有限数且处于地理范围内。
	 * @param latitude 纬度，单位度。
	 * @param longitude 经度，单位度。
	 * @return 两个值有限且纬度在 -90..90、经度在 -180..180 时为 true。
	 */
	bool validCoordinate(double latitude, double longitude) {
		return qIsFinite(latitude) && qIsFinite(longitude) && latitude >= -90.0 &&
			   latitude <= 90.0 && longitude >= -180.0 && longitude <= 180.0;
	}

#ifdef EVCHARGER_HAS_WEBENGINE
	/** @brief 构造腾讯选点器 URL，编码密钥和初始中心坐标。
	 * @param mapKey 腾讯地图服务密钥。
	 * @param coordinate 初始中心坐标。
	 * @return 已编码选点选项、坐标及密钥的 URL。
	 */
	QUrl pickerUrl(const QString &mapKey, const MapPickerDialog::Coordinate &coordinate) {
		QUrl url(QStringLiteral("https://apis.map.qq.com/tools/locpicker"));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("search"), QStringLiteral("1"));
		query.addQueryItem(QStringLiteral("type"), QStringLiteral("1"));
		query.addQueryItem(QStringLiteral("mapdraggable"), QStringLiteral("1"));
		query.addQueryItem(QStringLiteral("zoom"), QStringLiteral("15"));
		query.addQueryItem(QStringLiteral("coord"),
						   QStringLiteral("%1,%2")
							   .arg(QString::number(coordinate.latitude, 'f', 6),
									QString::number(coordinate.longitude, 'f', 6)));
		query.addQueryItem(QStringLiteral("coordtype"), QStringLiteral("5"));
		query.addQueryItem(QStringLiteral("key"), mapKey);
		query.addQueryItem(QStringLiteral("referer"), QStringLiteral("ev-charger"));
		url.setQuery(query);
		return url;
	}
#endif

} // namespace

/// @brief 校验初始坐标并建立地图选点器；无密钥或 WebEngine 时显示提示。
MapPickerDialog::MapPickerDialog(const QString &mapKey, double initialLatitude,
								 double initialLongitude, QWidget *parent, QSize windowSize)
	: QDialog(parent) {
	setObjectName(QStringLiteral("mapPickerDialog"));
	EV_LOG_DEBUG(mapPickerLog, this) << "MapPickerDialog initialized";
	setWindowTitle(tr("地图选点"));
	resize(windowSize);
	m_coordinate = validCoordinate(initialLatitude, initialLongitude)
					   ? Coordinate{initialLatitude, initialLongitude}
					   : Coordinate{kDefaultLatitude, kDefaultLongitude};

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(12, 12, 12, 12);
	layout->setSpacing(12);

#ifdef EVCHARGER_HAS_WEBENGINE
	if (!mapKey.trimmed().isEmpty()) {
		auto *map = new QWebEngineView(this);
		map->setObjectName(QStringLiteral("stationMapPicker"));
		layout->addWidget(map, 1);
		connect(map, &QWebEngineView::loadFinished, this, [this](bool ok) {
			if (!ok) {
				EV_LOG_WARNING(mapPickerLog, this) << "Map page loading failed";
			} else {
				EV_LOG_DEBUG(mapPickerLog, this) << "Map page loaded";
			}
		});

		connect(map, &QWebEngineView::titleChanged, this, [this](const QString &title) {
			const auto selected = coordinateFromTitle(title);
			if (!selected.has_value())
				return;
			EV_LOG_INFO(mapPickerLog, this) << "Station location selected";
			m_coordinate = *selected;
			accept();
		});

		const QString source = pickerUrl(mapKey, m_coordinate)
								   .toString(QUrl::FullyEncoded)
								   .toHtmlEscaped();
		// 仅信任选点 iframe 的两个腾讯来源；JavaScript 校验后以专用标题前缀传回 C++。
		// C++ 再检查有限数和经纬度范围，合法结果才接受对话框。
		const QString html = QStringLiteral(R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, minimum-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <style>html,body,iframe{width:100%;height:100%;margin:0;border:0;overflow:hidden;background:#1b1e23}</style>
</head>
<body>
  <iframe src="%1" allow="geolocation"></iframe>
  <script>
    window.addEventListener('message', function(event) {
      // The picker entry point redirects to mapapi.qq.com.
      if (event.origin !== 'https://apis.map.qq.com' &&
          event.origin !== 'https://mapapi.qq.com') return;
      if (event.source !== document.querySelector('iframe').contentWindow) return;
      const value = event.data;
      if (!value || value.module !== 'locationPicker' || !value.latlng) return;
      const lat = Number(value.latlng.lat);
      const lng = Number(value.latlng.lng);
      if (!Number.isFinite(lat) || !Number.isFinite(lng)) return;
      document.title = 'ev-charger-location:' + lat.toFixed(8) + ',' + lng.toFixed(8);
    }, false);
  </script>
</body>
</html>)HTML")
								 .arg(source);
		map->setHtml(html, QUrl(QStringLiteral("https://apis.map.qq.com/")));
	} else {
#endif
		EV_LOG_WARNING(mapPickerLog, this) << "Map picker unavailable; map key or WebEngine support missing";
		auto *unavailable = new QLabel(tr("地图服务未配置"), this);
		unavailable->setObjectName(QStringLiteral("mapUnavailableLabel"));
		unavailable->setAlignment(Qt::AlignCenter);
		layout->addWidget(unavailable, 1);
#ifdef EVCHARGER_HAS_WEBENGINE
	}
#else
	Q_UNUSED(mapKey)
#endif

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

std::optional<MapPickerDialog::Coordinate>
/// @brief 解析专用前缀的纬经度标题；格式、数值或范围错误返回 nullopt。
MapPickerDialog::coordinateFromTitle(const QString &title) {
	if (!title.startsWith(kSelectionPrefix))
		return std::nullopt;
	const QStringList values = title.mid(kSelectionPrefix.size()).split(QLatin1Char(','));
	if (values.size() != 2)
		return std::nullopt;
	bool latitudeOk = false;
	bool longitudeOk = false;
	const double latitude = values.at(0).toDouble(&latitudeOk);
	const double longitude = values.at(1).toDouble(&longitudeOk);
	if (!latitudeOk || !longitudeOk || !validCoordinate(latitude, longitude))
		return std::nullopt;
	return Coordinate{latitude, longitude};
}
