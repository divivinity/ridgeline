#include "LocationDialog.h"

#include "geo/Mgrs.h"

#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
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
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QElapsedTimer s_publicNominatimThrottle;

QString normalizedQuery(const QString& query) {
    return query.simplified().toLower();
}

QString geocoderEndpoint() {
    const QString configured = qEnvironmentVariable("RIDGELINE_GEOCODER_URL").trimmed();
    return configured.isEmpty()
        ? QStringLiteral("https://nominatim.openstreetmap.org/search")
        : configured;
}

QString precisionDescription(int precisionDigits) {
    static const int resolutions[] = {100000, 10000, 1000, 100, 10, 1};
    const int index = std::clamp(precisionDigits, 0, 5);
    return QStringLiteral("%1 m grid").arg(resolutions[index]);
}

} // namespace

LocationDialog::LocationDialog(double latitude,
                               double longitude,
                               double extentKm,
                               QWidget* parent)
    : QDialog(parent), m_network(new QNetworkAccessManager(this)) {
    setWindowTitle("Choose analysis location");
    setMinimumSize(640, 650);
    setModal(true);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(500);
    m_requestDelay = new QTimer(this);
    m_requestDelay->setSingleShot(true);

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
        "Search for a city, landmark, or address, or enter WGS84 or MGRS coordinates. "
        "Ridgeline downloads real elevation data and aligns imagery to the same bounds.");
    help->setWordWrap(true);
    help->setObjectName("DialogHelp");
    outer->addWidget(help);

    auto* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("LocationSearchEdit");
    m_searchEdit->setPlaceholderText("Search city, landmark, or address…");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchButton = new QPushButton("Search");
    m_searchButton->setObjectName("PrimarySmall");
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchButton);
    outer->addLayout(searchRow);

    m_searchStatus = new QLabel(usesPublicNominatim()
        ? "Cached places suggest as you type · press Search for a live public lookup."
        : "Type at least 3 characters; city suggestions appear after a short pause.");
    m_searchStatus->setObjectName("Muted");
    outer->addWidget(m_searchStatus);

    m_results = new QListWidget();
    m_results->setObjectName("SearchResults");
    m_results->setMinimumHeight(130);
    outer->addWidget(m_results, 1);

    auto* divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setObjectName("Divider");
    outer->addWidget(divider);

    auto* coordinateRow = new QFormLayout();
    coordinateRow->setHorizontalSpacing(18);
    coordinateRow->setVerticalSpacing(10);
    m_coordinateFormat = new QComboBox();
    m_coordinateFormat->setObjectName("CoordinateFormatCombo");
    m_coordinateFormat->addItem("WGS84 latitude / longitude");
    m_coordinateFormat->addItem("MGRS grid reference");
    coordinateRow->addRow("Coordinate input", m_coordinateFormat);
    outer->addLayout(coordinateRow);

    m_coordinateStack = new QStackedWidget();
    m_coordinateStack->setObjectName("CoordinateInputStack");

    auto* wgs84Page = new QWidget();
    auto* wgs84Form = new QFormLayout(wgs84Page);
    wgs84Form->setContentsMargins(0, 0, 0, 0);
    wgs84Form->setHorizontalSpacing(18);
    wgs84Form->setVerticalSpacing(10);
    m_latitude = new QDoubleSpinBox();
    m_latitude->setObjectName("LatitudeSpin");
    m_latitude->setRange(-85.0, 85.0);
    m_latitude->setDecimals(6);
    m_latitude->setSingleStep(0.001);
    m_latitude->setValue(latitude);
    m_longitude = new QDoubleSpinBox();
    m_longitude->setObjectName("LongitudeSpin");
    m_longitude->setRange(-180.0, 180.0);
    m_longitude->setDecimals(6);
    m_longitude->setSingleStep(0.001);
    m_longitude->setValue(longitude);
    wgs84Form->addRow("Latitude", m_latitude);
    wgs84Form->addRow("Longitude", m_longitude);
    m_coordinateStack->addWidget(wgs84Page);

    auto* mgrsPage = new QWidget();
    auto* mgrsLayout = new QVBoxLayout(mgrsPage);
    mgrsLayout->setContentsMargins(0, 0, 0, 0);
    mgrsLayout->setSpacing(7);
    auto* mgrsRow = new QHBoxLayout();
    m_mgrsEdit = new QLineEdit();
    m_mgrsEdit->setObjectName("MgrsEdit");
    m_mgrsEdit->setPlaceholderText("Example: 18S UJ 23394 07395");
    m_mgrsEdit->setClearButtonEnabled(true);
    auto* applyMgrsButton = new QPushButton("Use MGRS");
    applyMgrsButton->setObjectName("ApplyMgrsButton");
    mgrsRow->addWidget(m_mgrsEdit, 1);
    mgrsRow->addWidget(applyMgrsButton);
    mgrsLayout->addLayout(mgrsRow);
    m_mgrsStatus = new QLabel(
        "Spaces are optional. UTM-based MGRS bands C-X are supported.");
    m_mgrsStatus->setObjectName("Muted");
    m_mgrsStatus->setWordWrap(true);
    mgrsLayout->addWidget(m_mgrsStatus);
    m_coordinateStack->addWidget(mgrsPage);
    outer->addWidget(m_coordinateStack);

    auto* extentForm = new QFormLayout();
    extentForm->setHorizontalSpacing(18);
    extentForm->setVerticalSpacing(10);
    m_extent = new QDoubleSpinBox();
    m_extent->setObjectName("AnalysisWidthSpin");
    m_extent->setRange(2.0, 50.0);
    m_extent->setDecimals(0);
    m_extent->setSingleStep(5.0);
    m_extent->setSuffix(" km");
    m_extent->setValue(extentKm);
    extentForm->addRow("Analysis width", m_extent);
    outer->addLayout(extentForm);

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
    connect(m_searchEdit, &QLineEdit::textEdited,
            this, &LocationDialog::onSearchTextEdited);
    connect(m_searchDebounce, &QTimer::timeout, this, [this]() {
        requestSearch(m_searchEdit->text(), false);
    });
    connect(m_requestDelay, &QTimer::timeout,
            this, &LocationDialog::dispatchQueuedSearch);
    connect(m_results, &QListWidget::itemSelectionChanged,
            this, &LocationDialog::applySelectedResult);
    connect(m_results, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { applySelectedResult(); acceptLocation(); });
    connect(m_coordinateFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            m_coordinateStack, &QStackedWidget::setCurrentIndex);
    connect(applyMgrsButton, &QPushButton::clicked, this, &LocationDialog::applyMgrs);
    connect(m_mgrsEdit, &QLineEdit::returnPressed, this, &LocationDialog::applyMgrs);
    connect(m_mgrsEdit, &QLineEdit::textEdited, this, [this]() {
        m_mgrsStatus->setText("Press Use MGRS to convert this grid reference.");
    });
    connect(m_latitude, &QDoubleSpinBox::editingFinished, this,
            [this]() { m_locationName.clear(); });
    connect(m_longitude, &QDoubleSpinBox::editingFinished, this,
            [this]() { m_locationName.clear(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &LocationDialog::acceptLocation);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setStyleSheet(R"(
        QDialog { background: #0b1220; color: #e8eef7; }
        QLabel#Eyebrow { color: #5eead4; font-size: 10px; font-weight: 700; letter-spacing: 2px; }
        QLabel#DialogTitle { color: #f8fafc; font-size: 23px; font-weight: 650; }
        QLabel#DialogHelp { color: #94a3b8; font-size: 12px; line-height: 1.4; }
        QLabel#Muted, QLabel#Attribution { color: #64748b; font-size: 10px; }
        QLineEdit, QDoubleSpinBox, QComboBox {
            min-height: 34px; background: #111c2e; border: 1px solid #26364d;
            border-radius: 7px; padding: 2px 10px; color: #e8eef7; selection-background-color: #0f766e;
        }
        QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #2dd4bf; }
        QComboBox QAbstractItemView { background: #111c2e; color: #dbe7f5;
            selection-background-color: #0f766e; }
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

bool LocationDialog::usesPublicNominatim() const {
    return qEnvironmentVariable("RIDGELINE_GEOCODER_URL").trimmed().isEmpty();
}

QString LocationDialog::cacheKeyFor(const QString& query) const {
    const QString identity = usesPublicNominatim()
        ? QStringLiteral("public-nominatim") : geocoderEndpoint();
    const QByteArray cacheHash = QCryptographicHash::hash(
        (identity + QLatin1Char('\n') + normalizedQuery(query)).toUtf8(),
        QCryptographicHash::Sha256).toHex();
    return QStringLiteral("geocoder/v2/%1").arg(QString::fromLatin1(cacheHash));
}

void LocationDialog::setSearching(bool searching, const QString& message) {
    m_searchButton->setEnabled(!searching);
    // Keep the edit active during a request so newer text can replace the
    // pending typeahead query instead of making the control feel frozen.
    m_searchStatus->setText(message);
}

void LocationDialog::onSearchTextEdited(const QString& text) {
    m_searchDebounce->stop();
    const QString query = text.trimmed();
    if (query.size() < 3) {
        if (query.isEmpty()) m_results->clear();
        m_searchStatus->setText("Type at least 3 characters to see place suggestions.");
        return;
    }
    m_searchDebounce->start();
}

void LocationDialog::search() {
    m_searchDebounce->stop();
    requestSearch(m_searchEdit->text(), true);
}

void LocationDialog::requestSearch(const QString& rawQuery, bool userInitiated) {
    const QString query = rawQuery.simplified();
    if (query.isEmpty() || (!userInitiated && query.size() < 3)) return;

    QSettings settings;
    const QByteArray cached = settings.value(cacheKeyFor(query)).toByteArray();
    if (!cached.isEmpty()) {
        displaySearchResponse(cached);
        setSearching(false, "Showing cached suggestions · © OpenStreetMap contributors");
        return;
    }

    // The OSMF public service explicitly prohibits client-side autocomplete.
    // Live typeahead is therefore enabled for the configurable endpoint; the
    // public endpoint suggests only from Ridgeline's existing local cache and
    // remains available through the explicit Search action.
    if (usesPublicNominatim() && !userInitiated) {
        if (!displayCachedSuggestions(query)) {
            setSearching(false,
                "No cached match yet · press Search for a live public lookup.");
        }
        return;
    }

    m_pendingQuery = query;
    dispatchQueuedSearch();
}

void LocationDialog::dispatchQueuedSearch() {
    if (m_pendingQuery.isEmpty() || m_activeReply) return;

    const bool publicNominatim = usesPublicNominatim();
    if (publicNominatim && s_publicNominatimThrottle.isValid() &&
        s_publicNominatimThrottle.elapsed() < 1000) {
        const int waitMs = static_cast<int>(1000 - s_publicNominatimThrottle.elapsed());
        m_requestDelay->start(std::max(waitMs, 1));
        setSearching(true, QString("Public search queued · %1 ms").arg(waitMs));
        return;
    }

    const QString query = m_pendingQuery;
    m_pendingQuery.clear();

    QUrl url(geocoderEndpoint());
    QUrlQuery params;
    params.addQueryItem("q", query);
    params.addQueryItem("format", "jsonv2");
    params.addQueryItem("addressdetails", "1");
    params.addQueryItem("limit", "6");
    url.setQuery(params);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        "Ridgeline-RF-Planner/1.0 (https://github.com/divivinity/ridgeline)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(12000);
    setSearching(true, publicNominatim
        ? "Searching the world…" : "Loading place suggestions…");
    if (publicNominatim) s_publicNominatimThrottle.restart();

    m_activeReply = m_network->get(request);
    QNetworkReply* const reply = m_activeReply;
    const QString cacheKey = cacheKeyFor(query);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, query, cacheKey, publicNominatim]() {
        m_activeReply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            if (normalizedQuery(m_searchEdit->text()) == normalizedQuery(query)) {
                setSearching(false, QString("Search failed: %1").arg(reply->errorString()));
            } else if (m_pendingQuery.isEmpty()) {
                setSearching(false, "Type at least 3 characters to see place suggestions.");
            }
            reply->deleteLater();
            dispatchQueuedSearch();
            return;
        }

        const QByteArray response = reply->readAll();
        QSettings settings;
        settings.setValue(cacheKey, response);
        if (publicNominatim) {
            QStringList queries = settings.value("geocoder/v2/publicQueries").toStringList();
            const QString normalized = normalizedQuery(query);
            if (!queries.contains(normalized)) {
                queries.prepend(normalized);
                while (queries.size() > 100) queries.removeLast();
                settings.setValue("geocoder/v2/publicQueries", queries);
            }
        }

        if (normalizedQuery(m_searchEdit->text()) == normalizedQuery(query)) {
            displaySearchResponse(response);
            setSearching(false, "Select a suggestion below · © OpenStreetMap contributors");
        } else if (m_pendingQuery.isEmpty()) {
            setSearching(false, "Type at least 3 characters to see place suggestions.");
        }
        reply->deleteLater();
        dispatchQueuedSearch();
    });
}

bool LocationDialog::displayCachedSuggestions(const QString& query) {
    QSettings settings;
    const QString normalized = normalizedQuery(query);
    const QStringList cachedQueries =
        settings.value("geocoder/v2/publicQueries").toStringList();
    QJsonArray combined;
    QSet<QString> names;
    for (const QString& cachedQuery : cachedQueries) {
        if (!cachedQuery.startsWith(normalized)) continue;
        const QByteArray cached = settings.value(cacheKeyFor(cachedQuery)).toByteArray();
        const QJsonDocument document = QJsonDocument::fromJson(cached);
        if (!document.isArray()) continue;
        for (const QJsonValue& value : document.array()) {
            const QString name = value.toObject().value("display_name").toString();
            if (name.isEmpty() || names.contains(name)) continue;
            names.insert(name);
            combined.append(value);
            if (combined.size() >= 6) break;
        }
        if (combined.size() >= 6) break;
    }
    if (combined.isEmpty()) return false;
    displaySearchResponse(QJsonDocument(combined).toJson(QJsonDocument::Compact));
    setSearching(false, "Suggestions from your local search cache.");
    return true;
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
    m_coordinateFormat->setCurrentIndex(0);
}

void LocationDialog::applyMgrs() {
    MgrsCoordinate coordinate;
    std::string error;
    if (!mgrsToWgs84(m_mgrsEdit->text().toStdString(), coordinate, error)) {
        m_mgrsStatus->setStyleSheet("color: #fca5a5;");
        m_mgrsStatus->setText(QString::fromStdString(error));
        return;
    }

    m_latitude->setValue(coordinate.latitudeDeg);
    m_longitude->setValue(coordinate.longitudeDeg);
    m_locationName = QStringLiteral("MGRS %1").arg(m_mgrsEdit->text().simplified().toUpper());
    m_mgrsStatus->setStyleSheet("color: #5eead4;");
    m_mgrsStatus->setText(QString("%1°, %2° · %3")
        .arg(coordinate.latitudeDeg, 0, 'f', 6)
        .arg(coordinate.longitudeDeg, 0, 'f', 6)
        .arg(precisionDescription(coordinate.precisionDigits)));
}

void LocationDialog::acceptLocation() {
    if (m_coordinateFormat->currentIndex() == 1) {
        applyMgrs();
        MgrsCoordinate coordinate;
        std::string error;
        if (!mgrsToWgs84(m_mgrsEdit->text().toStdString(), coordinate, error)) return;
    }
    accept();
}
