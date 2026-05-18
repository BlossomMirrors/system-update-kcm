#include "softwareupdatebackend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QTextStream>

#include <KLocalizedString>

static constexpr auto RPMOSTREE_SERVICE       = "org.projectatomic.rpmostree1";
static constexpr auto RPMOSTREE_SYSROOT_PATH  = "/org/projectatomic/rpmostree1/Sysroot";
static constexpr auto RPMOSTREE_SYSROOT_IFACE = "org.projectatomic.rpmostree1.Sysroot";
static constexpr auto RPMOSTREE_OS_PATH       = "/org/projectatomic/rpmostree1/default";
static constexpr auto RPMOSTREE_OS_IFACE      = "org.projectatomic.rpmostree1.OS";
static constexpr auto SYSTEMD_SERVICE    = "org.freedesktop.systemd1";
static constexpr auto SYSTEMD_PATH       = "/org/freedesktop/systemd1";
static constexpr auto SYSTEMD_MGR_IFACE  = "org.freedesktop.systemd1.Manager";
static constexpr auto TIMER_UNIT         = "rpm-ostreed-automatic.timer";
static constexpr auto TIMER_PATH         =
    "/org/freedesktop/systemd1/unit/rpm_2dostreed_2dautomatic_2etimer";
static constexpr auto TXN_CONN_NAME      = "kcm-rpmostree-txn";

static QString readOsName()
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("Linux");
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("NAME="))) {
            QString name = line.mid(5);
            if (name.startsWith(QLatin1Char('"')))
                name = name.mid(1, name.length() - 2);
            return name;
        }
    }
    return QStringLiteral("Linux");
}

SoftwareUpdateBackend::SoftwareUpdateBackend(QObject *parent)
    : QObject(parent)
    , m_osName(readOsName())
{}

SoftwareUpdateBackend::~SoftwareUpdateBackend()
{
    cleanupTransaction();
}

void SoftwareUpdateBackend::checkForUpdates()
{
    fetchDeployments();
    readAutoUpdateState();
}

void SoftwareUpdateBackend::startUpgrade()
{
    if (m_busy) return;
    setBusy(true);
    setProgressPercent(0);
    setProgressMessage(QString());

    auto *iface = new QDBusInterface(
        QLatin1String(RPMOSTREE_SERVICE), QLatin1String(RPMOSTREE_OS_PATH),
        QLatin1String(RPMOSTREE_OS_IFACE), QDBusConnection::systemBus(), this);

    auto *w = new QDBusPendingCallWatcher(
        iface->asyncCall(QStringLiteral("Upgrade"), QVariantMap{}), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this, iface](QDBusPendingCallWatcher *watcher) {
        iface->deleteLater();
        watcher->deleteLater();
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT errorOccurred(reply.error().message());
            setBusy(false);
            Q_EMIT upgradeFinished(false);
            return;
        }
        beginTransaction(reply.value(), false);
    });
}

void SoftwareUpdateBackend::startRollback()
{
    if (m_busy) return;
    setBusy(true);
    setProgressPercent(0);
    setProgressMessage(QString());

    auto *iface = new QDBusInterface(
        QLatin1String(RPMOSTREE_SERVICE), QLatin1String(RPMOSTREE_OS_PATH),
        QLatin1String(RPMOSTREE_OS_IFACE), QDBusConnection::systemBus(), this);

    auto *w = new QDBusPendingCallWatcher(
        iface->asyncCall(QStringLiteral("Rollback"), QVariantMap{}), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this, iface](QDBusPendingCallWatcher *watcher) {
        iface->deleteLater();
        watcher->deleteLater();
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT errorOccurred(reply.error().message());
            setBusy(false);
            Q_EMIT rollbackFinished(false);
            return;
        }
        beginTransaction(reply.value(), true);
    });
}

void SoftwareUpdateBackend::scheduleUpgrade()
{
    setAutoUpdate(true);
}

void SoftwareUpdateBackend::cancelUpgrade()
{
    if (m_txnIface)
        m_txnIface->cancel();
    cleanupTransaction();
    setBusy(false);
}

void SoftwareUpdateBackend::setAutoUpdate(bool enabled)
{
    auto *mgr = new QDBusInterface(
        QLatin1String(SYSTEMD_SERVICE), QLatin1String(SYSTEMD_PATH),
        QLatin1String(SYSTEMD_MGR_IFACE), QDBusConnection::systemBus(), this);

    QDBusPendingCall call = enabled
        ? mgr->asyncCall(QStringLiteral("EnableUnitFiles"),
                         QStringList{QLatin1String(TIMER_UNIT)}, false, true)
        : mgr->asyncCall(QStringLiteral("DisableUnitFiles"),
                         QStringList{QLatin1String(TIMER_UNIT)}, false);

    auto *w = new QDBusPendingCallWatcher(call, this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, mgr, enabled](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (watcher->isError()) {
            mgr->deleteLater();
            Q_EMIT errorOccurred(watcher->error().message());
            return;
        }
        auto *rw = new QDBusPendingCallWatcher(
            mgr->asyncCall(QStringLiteral("Reload")), this);
        connect(rw, &QDBusPendingCallWatcher::finished, this, [this, mgr, enabled](QDBusPendingCallWatcher *rw2) {
            mgr->deleteLater();
            rw2->deleteLater();
            setAutoUpdateEnabled(enabled);
        });
    });
}

void SoftwareUpdateBackend::fetchDeployments()
{
    // Deployments is a property on the Sysroot object, not a method on the OS object.
    auto *iface = new QDBusInterface(
        QLatin1String(RPMOSTREE_SERVICE), QLatin1String(RPMOSTREE_SYSROOT_PATH),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QDBusConnection::systemBus(), this);

    auto *w = new QDBusPendingCallWatcher(
        iface->asyncCall(QStringLiteral("Get"),
                         QLatin1String(RPMOSTREE_SYSROOT_IFACE),
                         QStringLiteral("Deployments")), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this, iface](QDBusPendingCallWatcher *watcher) {
        iface->deleteLater();
        watcher->deleteLater();
        if (watcher->isError()) {
            Q_EMIT errorOccurred(watcher->error().message());
            return;
        }

        QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT errorOccurred(reply.error().message());
            return;
        }

        // Unwrap QDBusVariant → aa{sv} via QDBusArgument
        const QDBusArgument outerArg =
            qvariant_cast<QDBusArgument>(reply.value().variant());

        QList<QVariantMap> deployments;
        outerArg.beginArray();
        while (!outerArg.atEnd()) {
            QVariantMap map;
            outerArg >> map;
            deployments.append(map);
        }
        outerArg.endArray();

        QString current, pending, previous;
        for (const auto &dep : std::as_const(deployments)) {
            const QString version = dep.value(QStringLiteral("version")).toString();
            const bool booted     = dep.value(QStringLiteral("booted")).toBool();
            const bool staged     = dep.value(QStringLiteral("staged")).toBool();
            if (booted && current.isEmpty())
                current = version;
            else if (staged && pending.isEmpty())
                pending = version;
            else if (!booted && !staged && previous.isEmpty())
                previous = version;
        }
        setCurrentVersion(current);
        setPendingVersion(pending);
        setPreviousVersion(previous);
        setUpdateAvailable(!pending.isEmpty());
    });
}

void SoftwareUpdateBackend::readAutoUpdateState()
{
    auto *iface = new QDBusInterface(
        QLatin1String(SYSTEMD_SERVICE), QLatin1String(TIMER_PATH),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QDBusConnection::systemBus(), this);

    auto *w = new QDBusPendingCallWatcher(
        iface->asyncCall(QStringLiteral("Get"),
                         QStringLiteral("org.freedesktop.systemd1.Unit"),
                         QStringLiteral("ActiveState")),
        this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this, iface](QDBusPendingCallWatcher *watcher) {
        iface->deleteLater();
        watcher->deleteLater();
        QDBusPendingReply<QDBusVariant> reply = *watcher;
        const bool enabled = !reply.isError() &&
                             reply.value().variant().toString() == QLatin1String("active");
        setAutoUpdateEnabled(enabled);
    });
}

void SoftwareUpdateBackend::beginTransaction(const QString &address, bool isRollback)
{
    cleanupTransaction();
    m_txnIsRollback = isRollback;

    QDBusConnection conn =
        QDBusConnection::connectToPeer(address, QLatin1String(TXN_CONN_NAME));
    if (!conn.isConnected()) {
        Q_EMIT errorOccurred(i18n("Failed to connect to rpm-ostree transaction"));
        setBusy(false);
        if (isRollback) Q_EMIT rollbackFinished(false);
        else            Q_EMIT upgradeFinished(false);
        return;
    }
    m_txnConnectionName = QLatin1String(TXN_CONN_NAME);

    m_txnIface = new RpmOstreeTransaction(conn, this);
    connect(m_txnIface, &RpmOstreeTransaction::Message,
            this, &SoftwareUpdateBackend::onTxnMessage);
    connect(m_txnIface, &RpmOstreeTransaction::PercentProgress,
            this, &SoftwareUpdateBackend::onTxnProgress);
    connect(m_txnIface, &RpmOstreeTransaction::Finished,
            this, &SoftwareUpdateBackend::onTxnFinished);

    m_txnIface->start();
}

void SoftwareUpdateBackend::cleanupTransaction()
{
    delete m_txnIface;
    m_txnIface = nullptr;

    if (!m_txnConnectionName.isEmpty()) {
        QDBusConnection::disconnectFromPeer(m_txnConnectionName);
        m_txnConnectionName.clear();
    }
}

void SoftwareUpdateBackend::onTxnMessage(const QString &msg)
{
    setProgressMessage(msg);
}

void SoftwareUpdateBackend::onTxnProgress(quint32 percent)
{
    setProgressPercent(static_cast<int>(percent));
}

void SoftwareUpdateBackend::onTxnFinished(const QVariantMap &result)
{
    const bool success = result.value(QStringLiteral("success")).toBool();
    if (!success) {
        QString msg = result.value(QStringLiteral("error-message")).toString();
        if (msg.isEmpty())
            msg = i18n("Unknown error");
        Q_EMIT errorOccurred(msg);
    }

    const bool wasRollback = m_txnIsRollback;
    cleanupTransaction();
    setBusy(false);

    if (wasRollback) {
        Q_EMIT rollbackFinished(success);
    } else {
        if (success)
            fetchDeployments();
        Q_EMIT upgradeFinished(success);
    }
}

void SoftwareUpdateBackend::setCurrentVersion(const QString &v)
{
    if (m_currentVersion == v) return;
    m_currentVersion = v;
    Q_EMIT currentVersionChanged();
}

void SoftwareUpdateBackend::setPendingVersion(const QString &v)
{
    if (m_pendingVersion == v) return;
    m_pendingVersion = v;
    Q_EMIT pendingVersionChanged();
}

void SoftwareUpdateBackend::setPreviousVersion(const QString &v)
{
    if (m_previousVersion == v) return;
    m_previousVersion = v;
    Q_EMIT previousVersionChanged();
}

void SoftwareUpdateBackend::setProgressPercent(int v)
{
    if (m_progressPercent == v) return;
    m_progressPercent = v;
    Q_EMIT progressPercentChanged();
}

void SoftwareUpdateBackend::setProgressMessage(const QString &v)
{
    if (m_progressMessage == v) return;
    m_progressMessage = v;
    Q_EMIT progressMessageChanged();
}

void SoftwareUpdateBackend::setAutoUpdateEnabled(bool v)
{
    if (m_autoUpdateEnabled == v) return;
    m_autoUpdateEnabled = v;
    Q_EMIT autoUpdateEnabledChanged();
}

void SoftwareUpdateBackend::setUpdateAvailable(bool v)
{
    if (m_updateAvailable == v) return;
    m_updateAvailable = v;
    Q_EMIT updateAvailableChanged();
}

void SoftwareUpdateBackend::setBusy(bool v)
{
    if (m_busy == v) return;
    m_busy = v;
    Q_EMIT busyChanged();
}
