#include "LogBuffer.h"

#include "ShareText.h"

#include <QSysInfo>

#include <atomic>
#include <QDateTime>
#include <QGuiApplication>
#include <QJSEngine>
#include <QMetaObject>

namespace AetherSDR::mobile {

namespace {

constexpr int kMaxLines = 1000;
QtMessageHandler s_previousHandler = nullptr;
// False once the buffer is destroyed (application teardown): later messages,
// from any thread, must not reach the deleted object.
std::atomic<bool> s_bufferAlive{false};

bool worthKeeping(const QMessageLogContext& context, QtMsgType type, const QString& message)
{
    // Qt's iOS accessibility bridge repeats this ~30 times a second when the
    // system queries the UI; it says nothing about the app and flooded the
    // log (and the main thread with it).
    if (message.contains(QLatin1String("a11y element")) || message.contains(QLatin1String("invalid accessible interface"))) {
        return false;
    }
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        return true;
    }
    const QLatin1StringView category(context.category ? context.category : "");
    // The app's own categories, including aether.mobile.spectrum3d and
    // aether.mobile.audiosession.
    if (category == QLatin1StringView("aether.mobile") || category.startsWith(QLatin1StringView("aether.mobile."))) {
        return true;
    }
    // Commands sent to the radio, not its status stream.
    if (category == QLatin1StringView("aether.connection")) {
        return message.startsWith(QLatin1String("TX:"));
    }
    return category == QLatin1StringView("aether.protocol")
        && (message.contains(QLatin1String("xmit")) || message.contains(QLatin1String("PTT"))
            || message.contains(QLatin1String("display pan")) || message.contains(QLatin1String("refus")));
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (s_previousHandler) {
        s_previousHandler(type, context, message);
    }
    if (!s_bufferAlive.load() || !worthKeeping(context, type, message)) {
        return;
    }
    const QString line = QStringLiteral("%1 %2 %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
             QString::fromLatin1(context.category ? context.category : "default"),
             message.left(300));
    // Messages arrive from any thread; the buffer lives on the GUI thread.
    LogBuffer* buffer = LogBuffer::instance();
    QMetaObject::invokeMethod(buffer, [buffer, line] { buffer->append(line); }, Qt::QueuedConnection);
}

} // namespace

LogBuffer::~LogBuffer()
{
    s_bufferAlive = false;
    qInstallMessageHandler(s_previousHandler);
}

LogBuffer::LogBuffer(QObject* parent)
    : QObject(parent)
{
}

LogBuffer* LogBuffer::instance()
{
    static LogBuffer* buffer = new LogBuffer(qApp);
    return buffer;
}

LogBuffer* LogBuffer::create(QQmlEngine*, QJSEngine*)
{
    LogBuffer* buffer = instance();
    QJSEngine::setObjectOwnership(buffer, QJSEngine::CppOwnership);
    return buffer;
}

void LogBuffer::install()
{
    static_cast<void>(instance());
    s_bufferAlive = true;
    s_previousHandler = qInstallMessageHandler(messageHandler);
}

void LogBuffer::append(const QString& line)
{
    m_lines.append(line);
    emit lineAdded(line);
    if (m_lines.size() > kMaxLines) {
        m_lines.remove(0, m_lines.size() - kMaxLines);
    }
    emit linesChanged();
}

QString LogBuffer::report() const
{
    return QStringLiteral("AetherSDR Mobile %1 (build %2) — %3\n\n%4")
        .arg(QGuiApplication::applicationVersion(), QStringLiteral(AETHER_MOBILE_BUILD_NUMBER),
             QSysInfo::prettyProductName(), text());
}

void LogBuffer::copyToClipboard() const
{
    copyText(report());
}

bool LogBuffer::share() const
{
    return shareText(QStringLiteral("AetherSDR Mobile log"), report());
}

void LogBuffer::clear()
{
    m_lines.clear();
    emit linesChanged();
}

} // namespace AetherSDR::mobile
