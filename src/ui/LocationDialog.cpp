#include "LocationDialog.h"

#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>
#include <QVBoxLayout>

LocationDialog::LocationDialog(double latitude,
                               double longitude,
                               double extentKm,
                               QWidget* parent)
    : QDialog(parent), m_network(new QNetworkAccessManager(this)) {
    setWindowTitle("Choose analysis location");
    setMinimumSize(620, 560);
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 22, 24, 20);
    outer->setSpacing(14);

    auto* eyebrow = new QLabel("NEW ANALYSIS AREA");
    eyebrow->setObjectName("Eyebrow");
    outer->addWidget(eyebrow);
    auto* title = new QLabel("Where should Ridgeline calculate coverage?");
    title->setObjectName("DialogTitle");
    outer->addWidget(title);
    auto* help = new QLabel(
        "Search for any city, landmark, or address, or enter exact WGS84 coordinates. "
        "Ridgeline downloads real elevation data and aligns imagery to the same bounds.");
    help->setWordWrap(true);
    help->setObjectName("DialogHelp");
    outer->addWidget(help);

    auto* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search city, landmark, or address…");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchButton = new QPushButton("Search");
    m_searchButton->setObjectName("PrimarySmall");
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchButton);
    outer->addLayout(searchRow);

    m_searchStatus = new QLabel("Search is user-initiated and limited to protect the public service.");
    m_searchStatus->setObjectName("Muted");
    outer->addWidget(m_searchStatus);

    m_results = new QListWidget();
    m_results->setMinimumHeight(150);
    m_results->setObjectName("SearchResults");
    outer->addWidget(m_results, 1);

    auto* divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName("Divider");
    outer->addWidget(divider);

    auto* form = new QFormLayout();
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    m_latitude = new QDoubleSpinBox();
    m_latitude->setRange(-85.0, 85.0);
    m_latitude->setDecimals(6);
    m_latitude->setSingleStep(0.001);
    m_latitude->setValue(latitude);
    m_longitude = new QDoubleSpinBox();
    m_longitude->setRange(-180.0, 180.0);
    m_longitude->setDecimals(6);
    m_longitude->setSingleStep(0.001);
    m_longitude->setValue(longitude);
    m_extent = new QDoubleSpinBox();
    m_extent->setRange(2.0, 50.0);
    m_extent->setDecimals(0);
    m_extent->setSingleStep(5.0);
    m_extent->setSuffix(" km");
    m_extent->setValue(extentKm);
    form->addRow("Latitude", m_latitude);
    form->addRow("Longitude", m_longitude);
    form->addRow("Analysis width", m_extent);
    outer->addLayout(form);

    auto* source = new QLabel(
        "Elevation: Mapzen Terrain Tiles on AWS Open Data  ·  Place search: © OpenStreetMap contributors");
    source->setWordWrap(true);
    source->setObjectName("Attribution");
    outer->addWidget(source);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Load location");
    buttons->button(QDialogButtonBox::Ok)->setObjectName("PrimaryButton");
    outer->addWidget(buttons);

    connect(m_searchButton, &QPushButton::clicked, this, &LocationDialog::search);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &LocationDialog::search);
    connect(m_results, &QListWidget::itemSelectionChanged,
            this, &LocationDialog::applySelectedResult);
    connect(m_results, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { applySelectedResult(); accept(); });
    connect(m_latitude, &QDoubleSpinBox::editingFinished, this,
            [this]() { m_locationName.clear(); });
    connect(m_longitude, &QDoubleSpinBox::editingFinished, this,
            [this]() { m_locationName.clear(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setStyleSheet(R"(
        QDialog { background: #0b1220; color: #e8eef7; }
        QLabel#Eyebrow { color: #5eead4; font-size: 10px; font-weight: 700; letter-spacing: 2px; }
        QLabel#DialogTitle { color: #f8fafc; font-size: 23px; font-weight: 650; }
        QLabel#DialogHelp { color: #94a3b8; font-size: 12px; line-height: 1.4; }
        QLabel#Muted, QLabel#Attribution { color: #64748b; font-size: 10px; }
        QLineEdit, QDoubleSpinBox {
            min-height: 34px; background: #111c2e; border: 1px solid #26364d;
            border-radius: 7px; padding: 2px 10px; color: #e8eef7; selection-background-color: #0f766e;
        }
        QLineEdit:focus, QDoubleSpinBox:focus { border-color: #2dd4bf; }
        QListWidget#SearchResults {
            background: #0e1828; border: 1px solid #223148; border-radius: 9px;
            padding: 5px; outline: none;
        }
        QListWidget::item { padding: 9px; border-radius: 6px; color: #cbd5e1; }
        QListWidget::item:hover { background: #15243a; }
        QListWidget::item:selected { background: #123c43; color: #ecfeff; }
        QPushButton { min-height: 34px; padding: 0 16px; border-radius: 7px;
            border: 1px solid #2a3b52; background: #152238; color: #dbe7f5; }
        QPushButton:hover { background: #1c304b; border-color: #3b526e; }
        QPushButton#PrimarySmall, QPushButton#PrimaryButton {
            background: #0f766e; border-color: #14b8a6; color: white; font-weight: 650;
        }
        QPushButton#PrimarySmall:hover, QPushButton#PrimaryButton:hover { background: #0d9488; }
        QFrame#Divider { color: #23334a; }
    )");
}

double LocationDialog::latitude() const { return m_latitude->value(); }
double LocationDialog::longitude() const { return m_longitude->value(); }
double LocationDialog::extentKm() const { return m_extent->value(); }
QString LocationDialog::locationName() const { return m_locationName; }

void LocationDialog::setSearching(bool searching, const QString& message) {
    m_searchButton->setEnabled(!searching);
    m_searchEdit->setEnabled(!searching);
    m_searchStatus->setText(message);
}

void LocationDialog::search() {
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    const QByteArray cacheHash = QCryptographicHash::hash(query.toUtf8().toLower(),
                                                          QCryptographicHash::Sha256).toHex();
    const QString cacheKey = QStringLiteral("geocoder/%1").arg(QString::fromLatin1(cacheHash));
    QSettings settings;
    const QByteArray cached = settings.value(cacheKey).toByteArray();
    if (!cached.isEmpty()) {
        displaySearchResponse(cached);
        setSearching(false, "Showing a cached result · © OpenStreetMap contributors");
        return;
    }

    QString endpoint = qEnvironmentVariable("RIDGELINE_GEOCODER_URL");
    if (endpoint.isEmpty()) endpoint = "https://nominatim.openstreetmap.org/search";
    QUrl url(endpoint);
    QUrlQuery params;
    params.addQueryItem("q", query);
    params.addQueryItem("format", "jsonv2");
    params.addQueryItem("limit", "6");
    url.setQuery(params);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Ridgeline-RF-Planner/1.0 (https://github.com/divivinity/ridgeline)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(12000);
    setSearching(true, "Searching the world…");

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cacheKey]() {
        if (reply->error() != QNetworkReply::NoError) {
            setSearching(false, QString("Search failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const QByteArray response = reply->readAll();
        QSettings().setValue(cacheKey, response);
        displaySearchResponse(response);
        setSearching(false, "Select a result below · © OpenStreetMap contributors");
        reply->deleteLater();
    });
}

void LocationDialog::displaySearchResponse(const QByteArray& json) {
    m_results->clear();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray()) {
        m_searchStatus->setText("The search service returned an unexpected response.");
        return;
    }
    for (const QJsonValue& value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString name = object.value("display_name").toString();
        bool latOk = false, lonOk = false;
        const double lat = object.value("lat").toString().toDouble(&latOk);
        const double lon = object.value("lon").toString().toDouble(&lonOk);
        if (!latOk || !lonOk || name.isEmpty()) continue;
        auto* item = new QListWidgetItem(name, m_results);
        QVariantMap data;
        data.insert("lat", lat);
        data.insert("lon", lon);
        data.insert("name", name);
        item->setData(Qt::UserRole, data);
        item->setToolTip(QString("%1, %2").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
    }
    if (m_results->count() == 0)
        m_searchStatus->setText("No matching places found. Try a broader name or enter coordinates.");
    else
        m_results->setCurrentRow(0);
}

void LocationDialog::applySelectedResult() {
    QListWidgetItem* item = m_results->currentItem();
    if (!item) return;
    const QVariantMap data = item->data(Qt::UserRole).toMap();
    m_latitude->setValue(data.value("lat").toDouble());
    m_longitude->setValue(data.value("lon").toDouble());
    m_locationName = data.value("name").toString();
}
