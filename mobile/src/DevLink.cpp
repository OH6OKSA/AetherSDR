#include "DevLink.h"

#include "LogBuffer.h"
#include "MobileEngine.h"

#include "core/AppSettings.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QImageWriter>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSysInfo>

namespace AetherSDR::mobile {

namespace {
const QString kEnabledKey = QStringLiteral("MobileDevLink");
const QString kHostKey = QStringLiteral("MobileDevLinkHost");
constexpr int kRetryMs = 5000;
constexpr int kShotMaxWidth = 1400;
// Properties the PC may set. Receive and display only.
const QStringList kSettable = {
    QStringLiteral("spectrum3D"), QStringLiteral("wfAutoLevel"), QStringLiteral("wfBlackLevel"),
    QStringLiteral("wfColorGain"), QStringLiteral("traceAutoRange"), QStringLiteral("traceRefDbm"),
    QStringLiteral("traceRangeDb"), QStringLiteral("controlsVisible"), QStringLiteral("muted"),
    QStringLiteral("afGain"), QStringLiteral("locked"), QStringLiteral("cwDecoderEnabled"),
    QStringLiteral("meterLeft"), QStringLiteral("meterRight"),
};
} // namespace

DevLink* DevLink::instance()
{
    static DevLink* link = new DevLink(qApp);
    return link;
}

DevLink* DevLink::create(QQmlEngine*, QJSEngine*)
{
    DevLink* link = instance();
    QJSEngine::setObjectOwnership(link, QJSEngine::CppOwnership);
    return link;
}

DevLink::DevLink(QObject* parent)
    : QObject(parent)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, &DevLink::connectNow);
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        setStatus(QStringLiteral("connected"));
        send({{QStringLiteral("t"), QStringLiteral("hello")},
              {QStringLiteral("version"), QGuiApplication::applicationVersion()},
              {QStringLiteral("os"), QSysInfo::prettyProductName()},
              {QStringLiteral("device"), QSysInfo::machineHostName()}});
        // The log so far, then live lines.
        for (const QString& line : LogBuffer::instance()->lines()) {
            sendLogLine(line);
        }
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        if (enabled()) {
            setStatus(QStringLiteral("waiting"));
            m_retry.start();
        }
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (enabled()) {
            setStatus(QStringLiteral("waiting: ") + m_socket.errorString());
            m_socket.abort();
            m_retry.start();
        }
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &DevLink::onReadyRead);
    connect(LogBuffer::instance(), &LogBuffer::lineAdded, this, &DevLink::sendLogLine);
}

void DevLink::attach(MobileEngine* engine, QQmlApplicationEngine* qml)
{
    m_engine = engine;
    m_qml = qml;
    if (enabled()) {
        connectNow();
    }
}

bool DevLink::enabled() const
{
    return AppSettings::instance().value(kEnabledKey, QStringLiteral("False")).toString() == QLatin1String("True");
}

void DevLink::setEnabled(bool on)
{
    if (on == enabled()) {
        return;
    }
    AppSettings::instance().setValue(kEnabledKey, on ? QStringLiteral("True") : QStringLiteral("False"));
    AppSettings::instance().save();
    if (on) {
        connectNow();
    } else {
        m_retry.stop();
        m_socket.abort();
        setStatus(QStringLiteral("off"));
    }
    emit changed();
}

QString DevLink::host() const
{
    return AppSettings::instance().value(kHostKey, QString()).toString();
}

void DevLink::setHost(const QString& host)
{
    const QString h = host.trimmed();
    if (h == this->host()) {
        return;
    }
    AppSettings::instance().setValue(kHostKey, h);
    AppSettings::instance().save();
    emit changed();
    if (enabled()) {
        m_socket.abort();
        connectNow();
    }
}

void DevLink::connectNow()
{
    if (!enabled() || host().isEmpty()) {
        setStatus(enabled() ? QStringLiteral("no host") : QStringLiteral("off"));
        return;
    }
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    setStatus(QStringLiteral("connecting"));
    m_socket.connectToHost(host(), kPort);
}

void DevLink::setStatus(const QString& status)
{
    if (status != m_status) {
        m_status = status;
        emit changed();
    }
}

void DevLink::send(const QJsonObject& message)
{
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        m_socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    }
}

void DevLink::sendLogLine(const QString& line)
{
    send({{QStringLiteral("t"), QStringLiteral("log")}, {QStringLiteral("line"), line}});
}

void DevLink::onReadyRead()
{
    m_inbox += m_socket.readAll();
    qsizetype nl;
    while ((nl = m_inbox.indexOf('\n')) >= 0) {
        const QByteArray line = m_inbox.left(nl).trimmed();
        m_inbox.remove(0, nl + 1);
        if (line.isEmpty()) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) {
            handle(doc.object());
        }
    }
    if (m_inbox.size() > 65536) {
        m_inbox.clear();
    }
}

void DevLink::handle(const QJsonObject& command)
{
    const QString cmd = command.value(QStringLiteral("cmd")).toString();
    const QJsonValue id = command.value(QStringLiteral("id"));
    auto reply = [this, &cmd, &id](QJsonObject r) {
        r.insert(QStringLiteral("t"), QStringLiteral("reply"));
        r.insert(QStringLiteral("cmd"), cmd);
        r.insert(QStringLiteral("id"), id);
        send(r);
    };
    auto fail = [&reply](const QString& why) { reply({{QStringLiteral("error"), why}}); };

    if (cmd == QLatin1String("screenshot")) {
        screenshot(id);
    } else if (cmd == QLatin1String("state")) {
        reply(state());
    } else if (!m_engine) {
        fail(QStringLiteral("no engine"));
    } else if (cmd == QLatin1String("set")) {
        const QString prop = command.value(QStringLiteral("prop")).toString();
        if (!kSettable.contains(prop)) {
            fail(QStringLiteral("not settable: ") + prop);
            return;
        }
        m_engine->setProperty(prop.toUtf8().constData(), command.value(QStringLiteral("value")).toVariant());
        reply(state());
    } else if (cmd == QLatin1String("tune")) {
        m_engine->tuneTo(command.value(QStringLiteral("mhz")).toDouble());
        reply(state());
    } else if (cmd == QLatin1String("mode")) {
        m_engine->setMode(command.value(QStringLiteral("mode")).toString());
        reply(state());
    } else if (cmd == QLatin1String("band")) {
        m_engine->selectBand(command.value(QStringLiteral("band")).toString());
        reply(state());
    } else if (cmd == QLatin1String("zoom")) {
        m_engine->zoom(command.value(QStringLiteral("factor")).toDouble(1.0));
        reply(state());
    } else if (cmd == QLatin1String("drawer")) {
        // Opens or closes the controls drawer, optionally on a tab.
        if (!m_qml) {
            fail(QStringLiteral("no window"));
            return;
        }
        for (QObject* root : m_qml->rootObjects()) {
            if (QObject* drawer = root->findChild<QObject*>(QStringLiteral("controlsDrawer"))) {
                QMetaObject::invokeMethod(drawer, command.value(QStringLiteral("open")).toBool(true) ? "open" : "close");
            }
            if (command.contains(QStringLiteral("tab"))) {
                for (QObject* tabs : root->findChildren<QObject*>(QStringLiteral("controlsTabs"))) {
                    tabs->setProperty("currentIndex", command.value(QStringLiteral("tab")).toInt());
                }
            }
        }
        reply({{QStringLiteral("ok"), true}});
    } else {
        fail(QStringLiteral("unknown or not allowed: ") + cmd);
    }
}

QJsonObject DevLink::state() const
{
    QJsonObject s;
    if (!m_engine) {
        return s;
    }
    for (const char* name : {"connectionState", "radioName", "frequencyMhz", "mode", "filterLow", "filterHigh",
                             "panCenterMhz", "panBandwidthMhz", "spectrum3D", "transmitting", "transmitAvailable",
                             "transmitUnavailableReason", "sMeterDbm", "lastError", "locked", "controlsVisible"}) {
        s.insert(QLatin1String(name), QJsonValue::fromVariant(m_engine->property(name)));
    }
    return s;
}

void DevLink::screenshot(const QJsonValue& id)
{
    auto fail = [this, &id](const QString& why) {
        send({{QStringLiteral("t"), QStringLiteral("reply")}, {QStringLiteral("cmd"), QStringLiteral("screenshot")},
              {QStringLiteral("id"), id}, {QStringLiteral("error"), why}});
    };
    QQuickWindow* window = nullptr;
    if (m_qml && !m_qml->rootObjects().isEmpty()) {
        window = qobject_cast<QQuickWindow*>(m_qml->rootObjects().constFirst());
    }
    if (!window || !window->contentItem()) {
        fail(QStringLiteral("no window"));
        return;
    }
    // Popups and drawers live in the overlay, which is a child of the
    // content item, so they are in the grab too.
    const QSharedPointer<QQuickItemGrabResult> grab = window->contentItem()->grabToImage();
    if (!grab) {
        fail(QStringLiteral("grab refused (window not exposed?)"));
        return;
    }
    connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, grab, id] {
        QImage image = grab->image();
        if (image.width() > kShotMaxWidth) {
            image = image.scaledToWidth(kShotMaxWidth, Qt::SmoothTransformation);
        }
        // JPEG when the image plugin is there (smaller), PNG otherwise.
        const bool jpeg = QImageWriter::supportedImageFormats().contains("jpeg");
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, jpeg ? "JPG" : "PNG", jpeg ? 80 : -1);
        send({{QStringLiteral("t"), QStringLiteral("reply")}, {QStringLiteral("cmd"), QStringLiteral("screenshot")},
              {QStringLiteral("id"), id}, {QStringLiteral("width"), image.width()},
              {QStringLiteral("height"), image.height()},
              {QStringLiteral("format"), jpeg ? QStringLiteral("jpeg") : QStringLiteral("png")},
              {QStringLiteral("jpeg"), QString::fromLatin1(bytes.toBase64())}});
    });
}

} // namespace AetherSDR::mobile
