#pragma once

#include <QObject>
#include <QTimer>
#include <QVariant>

#include <functional>
#include <vector>

namespace AetherSDR::mobile {

class MobileEngine;

// Development audit (--echo-audit): for each radio-backed setting the app
// can change, set a nearby value, wait for the radio's status to bring it
// back, then restore the original and check that too. Logs one line per
// setting: OK, NO ECHO (the radio took it but the app still shows the old
// value) or RESTORE FAILED. Never keys the transmitter: VOX, antennas, MTU,
// callsign and name are left out.
class EchoAudit : public QObject {
    Q_OBJECT
public:
    explicit EchoAudit(MobileEngine& engine, QObject* parent = nullptr);
    void start();

signals:
    void finished();

private:
    struct Item {
        QString name;
        std::function<QVariant()> get;
        std::function<void(const QVariant&)> set;
        std::function<QVariant(const QVariant&)> next;
    };
    void build();
    void step();

    MobileEngine& m_engine;
    std::vector<Item> m_items;
    size_t m_index = 0;
    int m_phase = 0;  // 0 change, 1 check+restore, 2 check restore
    QVariant m_original;
    QVariant m_wanted;
    int m_noEcho = 0;
    int m_restoreFailed = 0;
    QTimer m_timer;
};

} // namespace AetherSDR::mobile
