#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>

#include "library_model.h"
#include "windows_integration.h"

LibraryModel::LibraryModel(QObject *parent) : QAbstractTableModel(parent) {
    icon_pool.setMaxThreadCount(2);
    icon_pool.setExpiryTimeout(5000);
}

LibraryModel::~LibraryModel() {
    icon_pool.clear();
    icon_pool.waitForDone();
}

int LibraryModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(records.size());
}

int LibraryModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : 3;
}

void LibraryModel::request_icon(const QString &path) const {
    if (pending_icons.contains(path) || pending_icons.size() >= 32 || path.isEmpty()) {
        return;
    }
    pending_icons.insert(path);
    auto *self = const_cast<LibraryModel *>(this);
    auto *watcher = new QFutureWatcher<QImage>(self);
    connect(watcher, &QFutureWatcher<QImage>::finished, self, [self, watcher, path] {
        const auto image = watcher->result();
        self->pending_icons.remove(path);
        self->icons.insert(path, new QIcon(image.isNull() ? QIcon{} : QIcon(QPixmap::fromImage(image))));
        watcher->deleteLater();
        for (int row = 0; row < self->records.size(); ++row) {
            if (self->records[row].path == path) {
                emit self->dataChanged(self->index(row, 0), self->index(row, 0), {Qt::DecorationRole});
            }
        }
    });
    watcher->setFuture(QtConcurrent::run(&self->icon_pool, [path] { return desktop_windows::application_icon(path); }));
}

QVariant LibraryModel::data(const QModelIndex &index, int requested_role) const {
    if (!index.isValid() || index.row() >= records.size() || index.row() < 0) {
        return {};
    }
    const auto &record = records[index.row()];
    switch (requested_role) {
    case Qt::DisplayRole:
        if (index.column() == 0)
            return record.name;
        if (index.column() == 1)
            return record.tags.join(", ");
        return record.running ? "Запущено" : record.available ? "Не запущено" : "Недоступно";
    case Qt::ToolTipRole:
        return record.name + '\n' + QDir::toNativeSeparators(record.path);
    case Qt::DecorationRole:
        if (index.column() == 0) {
            if (const auto *icon = icons.object(record.path))
                return *icon;
            request_icon(record.path);
        }
        return {};
    case id_role:
        return record.id;
    case record_role:
        return QVariant::fromValue(record);
    case tags_role:
        return record.tags;
    case running_role:
        return record.running;
    case favorite_role:
        return record.favorite;
    case available_role:
        return record.available;
    default:
        return {};
    }
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int requested_role) const {
    if (orientation == Qt::Horizontal && requested_role == Qt::DisplayRole) {
        return QStringList{"Приложение", "Теги", "Статус"}.value(section);
    }
    return {};
}

void LibraryModel::replace(QVector<app_record> applications) {
    beginResetModel();
    records = std::move(applications);
    search_names.clear();
    search_paths.clear();
    search_names.reserve(records.size());
    search_paths.reserve(records.size());
    for (const auto &record : records) {
        search_names.append(record.name.toCaseFolded());
        search_paths.append(QFileInfo(record.path).fileName().toCaseFolded());
    }
    endResetModel();
}

QHash<int, QByteArray> LibraryModel::roleNames() const {
    auto roles = QAbstractTableModel::roleNames();
    roles.insert(id_role, "application_id");
    roles.insert(record_role, "application_record");
    roles.insert(tags_role, "tags");
    roles.insert(running_role, "running");
    roles.insert(favorite_role, "favorite");
    roles.insert(available_role, "available");
    return roles;
}

void LibraryModel::update_running(const QSet<QString> &paths) {
    for (int row = 0; row < records.size(); ++row) {
        const bool running = paths.contains(QDir::fromNativeSeparators(records[row].path).toCaseFolded());
        if (records[row].running != running) {
            records[row].running = running;
            emit dataChanged(index(row, 0), index(row, 2), {Qt::DisplayRole, running_role, record_role});
        }
    }
}

const app_record *LibraryModel::find(qint64 id) const {
    for (const auto &record : records) {
        if (record.id == id)
            return &record;
    }
    return nullptr;
}

LibraryFilter::LibraryFilter(QObject *parent) : QSortFilterProxyModel(parent) {
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
    sort(0);
}

void LibraryFilter::set_query(const QString &query) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    terms = query.toCaseFolded().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}

void LibraryFilter::set_section(const QString &value, const QString &tag) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    section = value;
    selected_tag = tag;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}

void LibraryFilter::set_sort_mode(int value) {
    sort_mode = value;
    invalidate();
}

bool LibraryFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    if (parent.isValid())
        return false;
    const auto *model = static_cast<const LibraryModel *>(sourceModel());
    const auto &record = model->at(row);
    if (section == "favorites" && !record.favorite)
        return false;
    if (section == "running" && !record.running)
        return false;
    if (!selected_tag.isEmpty() && !record.tags.contains(selected_tag, Qt::CaseInsensitive))
        return false;
    for (const auto &term : terms) {
        if (term.startsWith("-#")) {
            if (record.tags.contains(term.mid(2), Qt::CaseInsensitive))
                return false;
        } else if (term.startsWith('#')) {
            if (!record.tags.contains(term.mid(1), Qt::CaseInsensitive))
                return false;
        } else if (!model->search_name(row).contains(term) && !model->search_path(row).contains(term)) {
            return false;
        }
    }
    return true;
}

bool LibraryFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const auto *model = static_cast<const LibraryModel *>(sourceModel());
    const auto &a = model->at(left.row());
    const auto &b = model->at(right.row());
    if (sort_mode == 1 && a.last_launch != b.last_launch)
        return a.last_launch > b.last_launch;
    if (sort_mode == 2 && a.favorite != b.favorite)
        return a.favorite;
    const auto comparison =
        QString::localeAwareCompare(model->search_name(left.row()), model->search_name(right.row()));
    return comparison == 0 ? a.id < b.id : comparison < 0;
}
