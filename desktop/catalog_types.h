#ifndef XLIBRARY_CATALOG_TYPES_H
#define XLIBRARY_CATALOG_TYPES_H

#include <atomic>
#include <QMetaType>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <stdexcept>

namespace catalog_limits {
inline constexpr qint64 json_bytes = 32 * 1024 * 1024;
inline constexpr qsizetype applications = 50000;
inline constexpr qsizetype text_length = 32768;
inline constexpr qsizetype tags_per_application = 256;
}

inline bool valid_tag_name(const QString &tag) {
    return !tag.isEmpty() && tag.size() <= 64 && !tag.contains(QChar::Null) &&
           !tag.contains(QRegularExpression("[\\s,#]"));
}

struct app_record {
    qint64 id = 0;
    QString name;
    QString path;
    QString arguments;
    QString working_directory;
    QStringList tags;
    bool favorite = false;
    bool available = true;
    bool running = false;
    qint64 last_launch = 0;
};

struct discovery_progress {
    int percent = -1;
    qsizetype processed = 0;
    qsizetype total = 0;
    QString stage;
};

enum class discovery_issue_kind { skipped, duplicate, error };

struct discovery_issue {
    QString source;
    QString reason;
    discovery_issue_kind kind = discovery_issue_kind::error;
};

struct catalog_import_result {
    qsizetype added = 0;
    qsizetype duplicates = 0;
};

enum class discovery_phase { running, cancelling, committing };

struct discovery_control {
    std::atomic_bool requested = false;
    std::atomic<discovery_phase> phase = discovery_phase::running;
};

class DiscoveryCancelled : public std::runtime_error {
public:
    DiscoveryCancelled() : std::runtime_error("Discovery cancelled") {}
};

struct scenario_step {
    qint64 application_id = 0;
    int delay_ms = 0;
    bool enabled = true;
};

struct scenario_record {
    qint64 id = 0;
    QString name;
    QVector<scenario_step> steps;
};

struct catalog_snapshot {
    QVector<app_record> applications;
    QStringList tags;
    QVector<scenario_record> scenarios;
};

Q_DECLARE_METATYPE(app_record)
Q_DECLARE_METATYPE(scenario_record)
Q_DECLARE_METATYPE(catalog_snapshot)

#endif
