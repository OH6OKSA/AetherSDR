#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace AetherSDR::mobile {

// The most recent log lines, shown on the in-app Log page so testers can
// copy what happened (PTT releases, zoom requests, commands sent to the
// radio) without a debugger. Radio status chatter is left out.
class LogBuffer : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList lines READ lines NOTIFY linesChanged)

public:
    static LogBuffer* instance();
    static LogBuffer* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // Installs the message handler; the previous handler still receives
    // every message.
    static void install();

    QStringList lines() const { return m_lines; }
    Q_INVOKABLE QString text() const { return m_lines.join(QLatin1Char('\n')); }
    // The log with a header (app version, OS) for a report.
    Q_INVOKABLE QString report() const;
    Q_INVOKABLE void copyToClipboard() const;
    // iOS share sheet (Mail, Messages, …); copies where there is none.
    Q_INVOKABLE bool share() const;
    Q_INVOKABLE void clear();
    // GUI thread only; the message handler queues calls here.
    void append(const QString& line);

signals:
    void linesChanged();
    void lineAdded(const QString& line);

private:
    // Private so QML must go through create() and share instance().
    explicit LogBuffer(QObject* parent);

    QStringList m_lines;
};

} // namespace AetherSDR::mobile
