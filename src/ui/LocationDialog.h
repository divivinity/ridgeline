#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QNetworkAccessManager;

class LocationDialog : public QDialog {
    Q_OBJECT
public:
    explicit LocationDialog(double latitude,
                            double longitude,
                            double extentKm,
                            QWidget* parent = nullptr);

    double latitude() const;
    double longitude() const;
    double extentKm() const;
    QString locationName() const;

private slots:
    void search();
    void applySelectedResult();

private:
    void displaySearchResponse(const QByteArray& json);
    void setSearching(bool searching, const QString& message = {});

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchButton = nullptr;
    QListWidget* m_results = nullptr;
    QDoubleSpinBox* m_latitude = nullptr;
    QDoubleSpinBox* m_longitude = nullptr;
    QDoubleSpinBox* m_extent = nullptr;
    QLabel* m_searchStatus = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QString m_locationName;
};
