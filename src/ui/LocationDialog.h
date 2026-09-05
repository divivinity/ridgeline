#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QNetworkAccessManager;
class QNetworkReply;
class QStackedWidget;
class QTimer;

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
    void onSearchTextEdited(const QString& text);
    void dispatchQueuedSearch();
    void applySelectedResult();
    void applyMgrs();
    void acceptLocation();

private:
    void requestSearch(const QString& query, bool userInitiated);
    bool displayCachedSuggestions(const QString& query);
    void displaySearchResponse(const QByteArray& json);
    void setSearching(bool searching, const QString& message = {});
    QString cacheKeyFor(const QString& query) const;
    bool usesPublicNominatim() const;

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_searchButton = nullptr;
    QListWidget* m_results = nullptr;
    QDoubleSpinBox* m_latitude = nullptr;
    QDoubleSpinBox* m_longitude = nullptr;
    QDoubleSpinBox* m_extent = nullptr;
    QComboBox* m_coordinateFormat = nullptr;
    QStackedWidget* m_coordinateStack = nullptr;
    QLineEdit* m_mgrsEdit = nullptr;
    QLabel* m_mgrsStatus = nullptr;
    QLabel* m_searchStatus = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_activeReply = nullptr;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_requestDelay = nullptr;
    QString m_pendingQuery;
    QString m_locationName;
};
