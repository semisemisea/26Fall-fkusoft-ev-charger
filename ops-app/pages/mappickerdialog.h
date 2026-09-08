#pragma once

#include <QDialog>

#include <optional>

class MapPickerDialog : public QDialog {
public:
	struct Coordinate {
		double latitude = 0.0;
		double longitude = 0.0;
	};

	MapPickerDialog(const QString &mapKey, double initialLatitude, double initialLongitude,
					QWidget *parent = nullptr);

	[[nodiscard]] double latitude() const { return m_coordinate.latitude; }
	[[nodiscard]] double longitude() const { return m_coordinate.longitude; }

	static std::optional<Coordinate> coordinateFromTitle(const QString &title);

private:
	Coordinate m_coordinate;
};
