#include "EchoAudit.h"

#include "MobileEngine.h"

#include <QDebug>
#include <QMetaProperty>

#include <cmath>

namespace AetherSDR::mobile {

namespace {

constexpr int kWaitMs = 2500;

// A nearby value of the same type: bools flip, numbers move by `delta`
// (down instead when up would pass `max`).
std::function<QVariant(const QVariant&)> nudge(double delta, double max = 1e9)
{
    return [delta, max](const QVariant& v) -> QVariant {
        if (v.typeId() == QMetaType::Bool) {
            return !v.toBool();
        }
        const double x = v.toDouble();
        const double y = (x + delta > max) ? x - delta : x + delta;
        if (v.typeId() == QMetaType::Double) {
            return y;
        }
        return int(std::lround(y));
    };
}

bool same(const QVariant& a, const QVariant& b)
{
    if (a.typeId() == QMetaType::Double || b.typeId() == QMetaType::Double) {
        return std::abs(a.toDouble() - b.toDouble()) < 1e-6;
    }
    return a == b || a.toString() == b.toString();
}

} // namespace

EchoAudit::EchoAudit(MobileEngine& engine, QObject* parent)
    : QObject(parent), m_engine(engine)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &EchoAudit::step);
}

void EchoAudit::build()
{
    MobileEngine* e = &m_engine;
    auto prop = [this, e](const char* name, std::function<QVariant(const QVariant&)> next) {
        const int idx = e->metaObject()->indexOfProperty(name);
        if (idx < 0) {
            qWarning() << "echo-audit: no property" << name;
            return;
        }
        const QMetaProperty p = e->metaObject()->property(idx);
        m_items.push_back({QString::fromLatin1(name), [e, p] { return p.read(e); },
                           [e, p](const QVariant& v) { p.write(e, v); }, std::move(next)});
    };
    // Slice
    prop("frequencyMhz", nudge(0.001));
    prop("mode", [](const QVariant& v) { return QVariant(v.toString() == QLatin1String("USB") ? "LSB" : "USB"); });
    prop("afGain", nudge(5, 100));
    prop("muted", nudge(1));
    prop("agcMode", [](const QVariant& v) { return QVariant(v.toString() == QLatin1String("fast") ? "med" : "fast"); });
    prop("agcThreshold", nudge(5, 100));
    prop("rfGain", [](const QVariant& v) { return QVariant(v.toInt() == 0 ? 8 : 0); });
    prop("ritOn", nudge(1));
    prop("ritHz", nudge(50, 9000));
    prop("xitOn", nudge(1));
    prop("xitHz", nudge(50, 9000));
    prop("squelchOn", nudge(1));
    prop("squelchLevel", nudge(5, 100));
    prop("locked", nudge(1));
    // CW
    prop("cwSpeed", nudge(2, 60));
    prop("cwPitch", nudge(50, 1000));
    prop("cwBreakIn", nudge(1));
    // Transmit settings (no keying)
    prop("txFilterLow", nudge(50, 500));
    prop("txFilterHigh", nudge(-100));
    prop("rfPower", nudge(-5));
    prop("tunePower", nudge(-1));
    prop("atuMemories", nudge(1));
    prop("procEnabled", nudge(1));
    prop("procLevel", [](const QVariant& v) { return QVariant((v.toInt() + 1) % 3); });
    prop("monitor", nudge(1));
    prop("monitorGain", nudge(5, 100));
    prop("radioMicLevel", nudge(5, 100));

    // DSP toggles and levels
    for (const QString& key : e->dspKeys()) {
        m_items.push_back({QStringLiteral("dsp ") + key, [e, key] { return QVariant(e->dspOn(key)); },
                           [e, key](const QVariant& v) { e->setDsp(key, v.toBool()); }, nudge(1)});
        if (e->dspHasLevel(key)) {
            m_items.push_back({QStringLiteral("dsp level ") + key, [e, key] { return QVariant(e->dspLevel(key)); },
                               [e, key](const QVariant& v) { e->setDspLevel(key, v.toInt()); }, nudge(5, 100)});
        }
    }
    // This app's slice: volume and pan
    for (const char* what : {"gain", "pan"}) {
        const QString w = QString::fromLatin1(what);
        m_items.push_back({QStringLiteral("slice ") + w,
                           [e, w] {
                               for (const QVariant& s : e->slices()) {
                                   const QVariantMap m = s.toMap();
                                   if (m.value(QStringLiteral("id")).toInt() == e->activeSliceId()) {
                                       return QVariant(m.value(w).toInt());
                                   }
                               }
                               return QVariant();
                           },
                           [e, w](const QVariant& v) { e->setSliceAudio(e->activeSliceId(), w, v.toDouble()); },
                           nudge(5, 100)});
    }
    // Phone (VOX on is left out: it could key from the radio's own mic)
    for (const char* key : {"voxLevel", "voxDelay", "dexpOn", "dexpLevel", "amCarrier"}) {
        const QString k = QString::fromLatin1(key);
        m_items.push_back({QStringLiteral("phone ") + k, [e, k] { return e->phone().value(k); },
                           [e, k](const QVariant& v) { e->setPhone(k, v.toInt()); },
                           k == QLatin1String("dexpOn") ? std::function<QVariant(const QVariant&)>(
                                                             [](const QVariant& v) { return QVariant(v.toInt() ? 0 : 1); })
                                                       : nudge(5, 100)});
    }
    // Panadapter display
    for (const char* key : {"average", "fps", "wfRate", "bandZoom"}) {
        const QString k = QString::fromLatin1(key);
        m_items.push_back({QStringLiteral("pan ") + k, [e, k] { return e->panDisplay().value(k); },
                           [e, k](const QVariant& v) { e->setPanDisplay(k, v.toInt()); },
                           k == QLatin1String("bandZoom")
                               ? std::function<QVariant(const QVariant&)>([](const QVariant& v) { return QVariant(v.toBool() ? 0 : 1); })
                               : nudge(5, 60)});
    }
    // Equalizer (receive): on/off and the first band
    m_items.push_back({QStringLiteral("eq rx on"), [e] { return QVariant(e->eqRxEnabled()); },
                       [e](const QVariant& v) { e->setEqEnabled(false, v.toBool()); }, nudge(1)});
    m_items.push_back({QStringLiteral("eq rx band 0"), [e] { return e->eqRxBands().value(0); },
                       [e](const QVariant& v) { e->setEqBand(false, 0, v.toInt()); }, nudge(2, 10)});
    // Radio Setup (callsign, name, tune mode and MTU left out)
    for (const char* key : {"remoteOn", "muteLocalWhenRemote", "maxPower", "txDelay", "interlockTimeoutS",
                            "cwDelay", "iambic", "swapPaddles", "micBias", "micBoost", "lineoutGain",
                            "lineoutMute", "headphoneGain", "headphoneMute"}) {
        const QString k = QString::fromLatin1(key);
        m_items.push_back({QStringLiteral("setup ") + k, [e, k] { return e->radioSetup().value(k); },
                           [e, k](const QVariant& v) { e->setRadioSetup(k, v); },
                           [](const QVariant& v) {
                               if (v.typeId() == QMetaType::Bool) {
                                   return QVariant(!v.toBool());
                               }
                               const int x = v.toInt();
                               return QVariant(x >= 95 ? x - 5 : x + 5);
                           }});
    }
}

void EchoAudit::start()
{
    build();
    qInfo().noquote() << "echo-audit: start," << m_items.size() << "settings";
    m_timer.start(100);
}

void EchoAudit::step()
{
    if (m_index >= m_items.size()) {
        qInfo().noquote() << QStringLiteral("echo-audit: done, %1 settings, %2 no echo, %3 restore failed")
                                 .arg(m_items.size()).arg(m_noEcho).arg(m_restoreFailed);
        emit finished();
        return;
    }
    Item& it = m_items[m_index];
    if (m_phase == 0) {
        m_original = it.get();
        if (!m_original.isValid()) {
            qInfo().noquote() << "echo-audit:" << it.name << "SKIPPED (no value)";
            ++m_index;
            m_timer.start(100);
            return;
        }
        m_wanted = it.next(m_original);
        it.set(m_wanted);
        m_phase = 1;
        m_timer.start(kWaitMs);
    } else if (m_phase == 1) {
        const QVariant now = it.get();
        const bool echoed = same(now, m_wanted);
        if (!echoed) {
            ++m_noEcho;
        }
        qInfo().noquote() << "echo-audit:" << it.name << (echoed ? "OK" : "NO ECHO") << "set"
                          << m_wanted.toString() << "reads" << now.toString() << "(was" << m_original.toString() << ")";
        it.set(m_original);
        m_phase = 2;
        m_timer.start(kWaitMs);
    } else {
        const QVariant now = it.get();
        if (!same(now, m_original)) {
            ++m_restoreFailed;
            qWarning().noquote() << "echo-audit:" << it.name << "RESTORE FAILED: reads" << now.toString()
                                 << "want" << m_original.toString();
        }
        m_phase = 0;
        ++m_index;
        m_timer.start(200);
    }
}

} // namespace AetherSDR::mobile
