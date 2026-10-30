#ifndef XLIBRARY_OPERATION_TYPES_H
#define XLIBRARY_OPERATION_TYPES_H

#include <QMetaType>
#include <QString>

enum class operation_state { loading, ready, error, cancelled, completed };

enum class operation_error_source { none, validation, sqlite, filesystem, platform, library, internal, updates };

inline QString operation_source_name(operation_error_source source) {
    switch (source) {
    case operation_error_source::none:
        return "none";
    case operation_error_source::validation:
        return "validation";
    case operation_error_source::sqlite:
        return "SQLite";
    case operation_error_source::filesystem:
        return "filesystem";
    case operation_error_source::platform:
        return "platform";
    case operation_error_source::library:
        return "library";
    case operation_error_source::internal:
        return "internal";
    case operation_error_source::updates:
        return "updates";
    }
    return "unknown";
}

struct operation_error {
    QString user_message;
    QString technical_reason;
    operation_error_source source = operation_error_source::none;
};

struct operation_result {
    quint64 id = 0;
    QString operation;
    operation_state state = operation_state::ready;
    operation_error error;
};

Q_DECLARE_METATYPE(operation_state)
Q_DECLARE_METATYPE(operation_error_source)
Q_DECLARE_METATYPE(operation_error)
Q_DECLARE_METATYPE(operation_result)

#endif
