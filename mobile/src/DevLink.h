#pragma once

#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlApplicationEngine;
class QQmlEngine;

namespace AetherSDR::mobile {

class MobileEngine;

// Developer link, off unless the user turns it on (Log page): the app
// connects out to a developer PC (host:47100, e.g. over the user's own VPN)
// and streams its log; the PC can ask for a screenshot or the app state and
// drive receive-side controls from a fixed allow-list. Nothing on it can
// transmit: PTT, TUNE, ATU, CW and CWX are not reachable.
class DevLink : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    static constexpr quint16 kPort = 47100;

    ~DevLink() override;
    static DevLink* instance();
    static DevLink* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    void attach(MobileEngine* engine, QQmlApplicationEngine* qml);

    bool enabled() const;
    void setEnabled(bool on);
    QString host() const;
    void setHost(const QString& host);
    QString status() const { return m_status; }

signals:
    void changed();

private:
    explicit DevLink(QObject* parent = nullptr);
    void connectNow();
    void setStatus(const QString& status);
    void send(const QJsonObject& message);
    void onReadyRead();
    void handle(const QJsonObject& command);
    QJsonObject state() const;
    // Replies asynchronously (grabToImage): grabWindow did not return on iOS/Metal.
    void screenshot(const QJsonValue& id);
    void sendLogLine(const QString& line);

    QPointer<MobileEngine> m_engine;
    QQmlApplicationEngine* m_qml{nullptr};
    QTcpSocket m_socket;
    QTimer m_retry;
    QByteArray m_inbox;
    QString m_status{QStringLiteral("off")};
};

} // namespace AetherSDR::mobile
