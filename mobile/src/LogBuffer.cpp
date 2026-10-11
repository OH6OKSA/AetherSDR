#include "LogBuffer.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QJSEngine>
#include <QMetaObject>

namespace AetherSDR::mobile {

namespace {

constexpr int kMaxLines = 400;
QtMessageHandler s_previousHandler = nullptr;

bool worthKeeping(const QMessageLogContext& context, QtMsgType type, const QString& message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        return true;
    }
    const QLatin1StringView category(context.category ? context.category : "");
    if (category == QLatin1StringView("aether.mobile")) {
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
    if (!worthKeeping(context, type, message)) {
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
    s_previousHandler = qInstallMessageHandler(messageHandler);
}

void LogBuffer::append(const QString& line)
{
    m_lines.append(line);
    if (m_lines.size() > kMaxLines) {
        m_lines.remove(0, m_lines.size() - kMaxLines);
    }
    emit linesChanged();
}

void LogBuffer::copyToClipboard() const
{
    QGuiApplication::clipboard()->setText(text());
}

void LogBuffer::clear()
{
    m_lines.clear();
    emit linesChanged();
}

} // namespace AetherSDR::mobile
