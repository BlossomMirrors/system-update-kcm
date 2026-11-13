#include "softwareupdatebackend.h"
#include "rpmostreetransaction.h"
#include "updatehelperdbus.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextStream>
#include <QTimer>

#include <KFormat>
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
static constexpr auto JOBVIEW_SERVICE      = "org.kde.JobViewServer";
static constexpr auto JOBVIEW_SERVER_PATH  = "/JobViewServer";
static constexpr auto JOBVIEW_SERVER_IFACE = "org.kde.JobViewServer";
static constexpr auto JOBVIEW_IFACE        = "org.kde.JobViewV2";

// Extract a KEY=value or KEY="value" line from os-release formatted text
static QString parseOsReleaseField(QTextStream &in, const QString &key)
{
    const QString prefix = key + QLatin1Char('=');
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith(prefix)) {
            QString value = line.mid(prefix.length());
            if (value.startsWith(QLatin1Char('"')))
                value = value.mid(1, value.length() - 2);
            return value;
        }
    }
    return {};
}

static QString readOsName()
{
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("Linux");
    QTextStream in(&f);
    const QString name = parseOsReleaseField(in, QStringLiteral("PRETTY_NAME"));
    return name.isEmpty() ? QStringLiteral("Linux") : name;
}

/*  The identity repo's os-release always tracks whatever release is next,
 *  so its PRETTY_NAME is the name of the pending update regardless of which
 *  image origin produced it
*/
static constexpr auto IDENTITY_OS_RELEASE_URL =
    "https://dev.blossomos.org/blossom/os/core/patches/identity/-/raw/main/os-release";

SoftwareUpdateBackend::SoftwareUpdateBackend(QObject *parent)
    : QObject(parent)
    , m_osName(readOsName())
{
    /*  Upgrades run in the update helper so they survive closing the window;
     *  listen to it regardless of who started the upgrade
    */
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QLatin1String(HELPER_SERVICE), QLatin1String(HELPER_PATH),
                QLatin1String(HELPER_IFACE), QStringLiteral("StateChanged"),
                this, SLOT(onHelperStateChanged(QVariantMap)));
    bus.connect(QLatin1String(HELPER_SERVICE), QLatin1String(HELPER_PATH),
                QLatin1String(HELPER_IFACE), QStringLiteral("Finished"),
                this, SLOT(onHelperFinished(bool,bool,QString)));

    m_helperWatcher = new QDBusServiceWatcher(
        QLatin1String(HELPER_SERVICE), bus,
        QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_helperWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        // The helper crashed or was killed without reporting a result
        if (!m_helperUpgrade)
            return;
        endHelperUpgrade();
        checkForUpdates();
    });
}

/*  The locally installed system's own name, independent of osName which may
 *  be showing a pending update's remote branding instead
*/
QString SoftwareUpdateBackend::installedOsName() const
{
    return readOsName();
}

SoftwareUpdateBackend::~SoftwareUpdateBackend()
{
    closeJobView(QString());
    cleanupTransaction();
}

// Build a system-bus method call with interactive Polkit authorization enabled.
static QDBusPendingCall authCall(const char *service, const char *path,
                                 const char *iface, const QString &method,
                                 const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(service), QLatin1String(path), QLatin1String(iface), method);
    msg.setArguments(args);
    msg.setInteractiveAuthorizationAllowed(true);
    return QDBusConnection::systemBus().asyncCall(msg);
}

void SoftwareUpdateBackend::checkForUpdates()
{
    if (m_busy || m_checking) return;
    setChecking(true);
    readAutoUpdateState();

    /*  An upgrade may still be running from an earlier session of this
     *  window; ask without starting the helper just for the question
    */
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(HELPER_SERVICE), QLatin1String(HELPER_PATH),
        QLatin1String(HELPER_IFACE), QStringLiteral("GetState"));
    msg.setAutoStartService(false);
    auto *w = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *watcher;
        if (!reply.isError() && reply.value().value(QStringLiteral("running")).toBool()) {
            setChecking(false);
            attachToHelper(reply.value());
            return;
        }
        runCheck();
    });
}

void SoftwareUpdateBackend::runCheck()
{

    const QVariantMap options{{QStringLiteral("mode"), QStringLiteral("check")}};
    auto *w = new QDBusPendingCallWatcher(
        authCall(RPMOSTREE_SERVICE, RPMOSTREE_OS_PATH, RPMOSTREE_OS_IFACE,
                 QStringLiteral("AutomaticUpdateTrigger"),
                 {QVariant::fromValue(options)}), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<bool, QString> reply = *watcher;
        if (reply.isError()) {
            // Fall back to showing the local state only
            fetchDeployments();
            return;
        }
        beginTransaction(reply.argumentAt<1>(), TxnKind::Check);
    });
}

void SoftwareUpdateBackend::startUpgrade()
{
    if (m_busy) return;
    m_helperUpgrade = true;
    setBusy(true);
    resetProgress();

    // The helper is D-Bus activated and keeps going once this window closes
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(HELPER_SERVICE), QLatin1String(HELPER_PATH),
        QLatin1String(HELPER_IFACE), QStringLiteral("Upgrade"));
    msg.setArguments({m_osName, m_pendingVersion,
                      QVariant::fromValue<qulonglong>(m_downloadBytes)});

    auto *w = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (!watcher->isError() || !m_helperUpgrade)
            return;
        endHelperUpgrade();
        Q_EMIT errorOccurred(watcher->error().message());
        Q_EMIT upgradeFinished(false);
    });
}

void SoftwareUpdateBackend::startRollback()
{
    if (m_busy) return;
    setBusy(true);
    resetProgress();

    auto *w = new QDBusPendingCallWatcher(
        authCall(RPMOSTREE_SERVICE, RPMOSTREE_OS_PATH, RPMOSTREE_OS_IFACE,
                 QStringLiteral("Rollback"), {QVariant::fromValue(QVariantMap{})}), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT errorOccurred(reply.error().message());
            setBusy(false);
            Q_EMIT rollbackFinished(false);
            return;
        }
        beginTransaction(reply.value(), TxnKind::Rollback);
    });
}

void SoftwareUpdateBackend::startReset()
{
    if (m_busy) return;
    setBusy(true);
    setResetting(true);
    resetProgress();

    // Same options the rpm-ostree reset CLI passes to UpdateDeployment
    const QVariantMap options{
        {QStringLiteral("no-pull-base"), true},
        {QStringLiteral("cache-only"), true},
        {QStringLiteral("no-layering"), true},
        {QStringLiteral("no-overrides"), true},
        {QStringLiteral("no-initramfs"), true},
    };

    auto *w = new QDBusPendingCallWatcher(
        authCall(RPMOSTREE_SERVICE, RPMOSTREE_OS_PATH, RPMOSTREE_OS_IFACE,
                 QStringLiteral("UpdateDeployment"),
                 {QVariant::fromValue(QVariantMap{}), QVariant::fromValue(options)}), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT errorOccurred(reply.error().message());
            setBusy(false);
            setResetting(false);
            return;
        }
        beginTransaction(reply.value(), TxnKind::Reset);
    });
}

void SoftwareUpdateBackend::scheduleUpgrade()
{
    setAutoUpdate(true);
}

void SoftwareUpdateBackend::cancelUpgrade()
{
    if (m_helperUpgrade) {
        // The helper reports back through Finished once the daemon stopped
        setCancelling(true);
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QLatin1String(HELPER_SERVICE), QLatin1String(HELPER_PATH),
            QLatin1String(HELPER_IFACE), QStringLiteral("Cancel"));
        msg.setAutoStartService(false);
        QDBusConnection::sessionBus().asyncCall(msg);
        return;
    }
    if (m_txnIface)
        m_txnIface->cancel();
    closeJobView(QString());
    cleanupTransaction();
    setBusy(false);
    setResetting(false);
}

void SoftwareUpdateBackend::setAutoUpdate(bool enabled)
{
    if (enabled) {
        QVariantList enableArgs = { QVariant(QStringList{QLatin1String(TIMER_UNIT)}),
                                    QVariant(false), QVariant(true) };
        auto *w = new QDBusPendingCallWatcher(
            authCall(SYSTEMD_SERVICE, SYSTEMD_PATH, SYSTEMD_MGR_IFACE,
                     QStringLiteral("EnableUnitFiles"), enableArgs), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            if (watcher->isError()) {
                // Usually a dismissed auth prompt; snap the switch back quietly
                readAutoUpdateState();
                return;
            }
            auto *rw = new QDBusPendingCallWatcher(
                authCall(SYSTEMD_SERVICE, SYSTEMD_PATH, SYSTEMD_MGR_IFACE,
                         QStringLiteral("Reload")), this);
            connect(rw, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *rw2) {
                rw2->deleteLater();
                QVariantList startArgs = { QVariant(QLatin1String(TIMER_UNIT)),
                                           QVariant(QStringLiteral("replace")) };
                auto *sw = new QDBusPendingCallWatcher(
                    authCall(SYSTEMD_SERVICE, SYSTEMD_PATH, SYSTEMD_MGR_IFACE,
                             QStringLiteral("StartUnit"), startArgs), this);
                connect(sw, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *sw2) {
                    sw2->deleteLater();
                    readAutoUpdateState();
                });
            });
        });
    } else {
        QVariantList stopArgs = { QVariant(QLatin1String(TIMER_UNIT)),
                                  QVariant(QStringLiteral("replace")) };
        auto *w = new QDBusPendingCallWatcher(
            authCall(SYSTEMD_SERVICE, SYSTEMD_PATH, SYSTEMD_MGR_IFACE,
                     QStringLiteral("StopUnit"), stopArgs), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            if (watcher->isError()) {
                // Usually a dismissed auth prompt; snap the switch back quietly
                readAutoUpdateState();
                return;
            }
            QVariantList disableArgs = { QVariant(QStringList{QLatin1String(TIMER_UNIT)}),
                                         QVariant(false) };
            auto *rw = new QDBusPendingCallWatcher(
                authCall(SYSTEMD_SERVICE, SYSTEMD_PATH, SYSTEMD_MGR_IFACE,
                         QStringLiteral("DisableUnitFiles"), disableArgs), this);
            connect(rw, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *rw2) {
                rw2->deleteLater();
                auto *sw = new QDBusPendingCallWatcher(
                    authCall(SYSTEMD_SERVICE, SYSTEMD_PATH, SYSTEMD_MGR_IFACE,
                             QStringLiteral("Reload")), this);
                connect(sw, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *sw2) {
                    sw2->deleteLater();
                    readAutoUpdateState();
                });
            });
        });
    }
}

void SoftwareUpdateBackend::rebootSystem()
{
    auto *w = new QDBusPendingCallWatcher(
        authCall("org.freedesktop.login1", "/org/freedesktop/login1",
                 "org.freedesktop.login1.Manager",
                 QStringLiteral("Reboot"), {QVariant(true)}), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (!watcher->isError())
            return;
        // Connection loss just means the shutdown is already underway
        const QDBusError::ErrorType type = watcher->error().type();
        if (type == QDBusError::Disconnected || type == QDBusError::NoReply
            || type == QDBusError::TimedOut)
            return;
        Q_EMIT errorOccurred(watcher->error().message());
    });
}

/*  Shows Plasma's shutdown screen with restart preselected, so the user can
 *  still cancel and open apps get a chance to save their state
*/
void SoftwareUpdateBackend::requestReboot()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("/LogoutPrompt"),
        QStringLiteral("org.kde.LogoutPrompt"), QStringLiteral("promptReboot"));
    auto *w = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        // Outside a Plasma session there is no prompt to show
        if (watcher->isError())
            rebootSystem();
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
            setChecking(false);
            Q_EMIT errorOccurred(watcher->error().message());
            return;
        }

        QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (reply.isError()) {
            setChecking(false);
            Q_EMIT errorOccurred(reply.error().message());
            return;
        }

        // Unwrap QDBusVariant: aa{sv} via QDBusArgument
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

        // Extract fields from base-commit-meta (nested a{sv})
        auto commitMeta = [](const QVariantMap &dep) -> QVariantMap {
            const QVariant v = dep.value(QStringLiteral("base-commit-meta"));
            if (!v.canConvert<QDBusArgument>()) return {};
            QVariantMap meta;
            qvariant_cast<QDBusArgument>(v) >> meta;
            return meta;
        };
        auto imageDigest = [&commitMeta](const QVariantMap &dep) -> QString {
            return commitMeta(dep).value(QStringLiteral("ostree.manifest-digest")).toString();
        };

        auto stringList = [](const QVariant &v) -> QStringList {
            if (v.canConvert<QDBusArgument>()) {
                QStringList l;
                qvariant_cast<QDBusArgument>(v) >> l;
                return l;
            }
            return v.toStringList();
        };

        auto layeredOf = [&stringList](const QVariantMap &dep) {
            QStringList l = stringList(dep.value(QStringLiteral("packages")))
                          + stringList(dep.value(QStringLiteral("requested-local-packages")));
            l.removeDuplicates();
            return l;
        };

        QString current, pending, previous;
        QString bootedDigest, pendingDigest, pendingChecksum;
        QStringList layeredBooted, layeredPending;
        bool hasPending = false;
        int bootedIdx = -1;

        for (int i = 0; i < deployments.size(); ++i) {
            const auto &dep = deployments.at(i);
            if (dep.value(QStringLiteral("booted")).toBool()) {
                bootedIdx = i;
                current = dep.value(QStringLiteral("version")).toString();
                bootedDigest = imageDigest(dep);
                m_bootedChecksum = dep.value(QStringLiteral("checksum")).toString();
                layeredBooted = layeredOf(dep);
                break;
            }
        }

        for (int i = 0; i < deployments.size(); ++i) {
            const auto &dep = deployments.at(i);
            if (dep.value(QStringLiteral("booted")).toBool())
                continue;
            const QString version = dep.value(QStringLiteral("version")).toString();
            const bool staged     = dep.value(QStringLiteral("staged")).toBool();
            const bool beforeBooted = i < bootedIdx;
            if ((staged || beforeBooted) && pending.isEmpty()) {
                pending = version;
                pendingDigest = imageDigest(dep);
                pendingChecksum = dep.value(QStringLiteral("checksum")).toString();
                layeredPending = layeredOf(dep);
                hasPending = true;
            } else if (!staged && !beforeBooted && previous.isEmpty()) {
                previous = version;
            }
        }

        // Plain ostree commits carry no image digest, so compare checksums
        const bool depUpdateAvailable = hasPending
            && (pendingDigest != bootedDigest
                || (pendingDigest.isEmpty() && pendingChecksum != m_bootedChecksum));
        setCurrentVersion(current);
        setPendingVersion(pending);
        setPreviousVersion(previous);
        // The pending deployment reflects what the next boot and future updates use
        setLayeredPackages(hasPending ? layeredPending : layeredBooted);
        setUpdateAvailable(depUpdateAvailable);
        // Already deployed for the next boot, so only a restart is left
        setUpdateStaged(depUpdateAvailable);
        if (depUpdateAvailable)
            fetchRemoteOsName();
        fetchCachedUpdate();
    });
}

void SoftwareUpdateBackend::fetchCachedUpdate()
{
    auto *iface = new QDBusInterface(
        QLatin1String(RPMOSTREE_SERVICE), QLatin1String(RPMOSTREE_OS_PATH),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QDBusConnection::systemBus(), this);

    auto *w = new QDBusPendingCallWatcher(
        iface->asyncCall(QStringLiteral("Get"),
                         QLatin1String(RPMOSTREE_OS_IFACE),
                         QStringLiteral("CachedUpdate")), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this, iface](QDBusPendingCallWatcher *watcher) {
        iface->deleteLater();
        watcher->deleteLater();
        /*  The deployment list already resolved a stronger signal; do not
         *  reissue the remote name lookup or fall back to the local name
        */
        const bool alreadyAvailable = m_updateAvailable;
        QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (!reply.isError()) {
            QVariantMap update;
            const QVariant v = reply.value().variant();
            if (v.canConvert<QDBusArgument>())
                qvariant_cast<QDBusArgument>(v) >> update;

            /*  Container images keep the same version label across builds,
             *  so a new commit only shows up as a checksum or layer change
            */
            const QString checksum = update.value(QStringLiteral("checksum")).toString();
            quint64 changedLayers = 0;
            quint64 addedSize = 0;
            const QVariant diffVar = update.value(QStringLiteral("manifest-diff"));
            if (diffVar.canConvert<QDBusArgument>()) {
                QVariantMap diff;
                qvariant_cast<QDBusArgument>(diffVar) >> diff;
                changedLayers = diff.value(QStringLiteral("n-added")).toULongLong()
                              + diff.value(QStringLiteral("n-removed")).toULongLong();
                addedSize = diff.value(QStringLiteral("added-size")).toULongLong();
            }

            const bool available = !update.isEmpty()
                && (update.value(QStringLiteral("ref-has-new-commit")).toBool()
                    || changedLayers > 0
                    || (!checksum.isEmpty() && !m_bootedChecksum.isEmpty()
                        && checksum != m_bootedChecksum));
            if (available) {
                const QString version = update.value(QStringLiteral("version")).toString();
                if (m_pendingVersion.isEmpty() && !version.isEmpty())
                    setPendingVersion(version);
                setUpdateAvailable(true);
                if (!alreadyAvailable)
                    fetchRemoteOsName();
            } else if (!alreadyAvailable) {
                setOsName(readOsName());
            }
            m_downloadBytes = available ? addedSize : 0;
            setDownloadSize(available && addedSize > 0
                ? QLocale().formattedDataSize(static_cast<qint64>(addedSize))
                : QString());
        } else if (!alreadyAvailable) {
            setOsName(readOsName());
        }
        setChecking(false);
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
        // Always notify so the switch snaps back after a dismissed auth prompt
        m_autoUpdateEnabled = enabled;
        Q_EMIT autoUpdateEnabledChanged();
    });
}

void SoftwareUpdateBackend::beginTransaction(const QString &address, TxnKind kind)
{
    cleanupTransaction();
    m_txnKind = kind;

    QDBusConnection conn =
        QDBusConnection::connectToPeer(address, QLatin1String(TXN_CONN_NAME));
    if (!conn.isConnected()) {
        Q_EMIT errorOccurred(i18n("Failed to connect to rpm-ostree transaction"));
        setBusy(false);
        setResetting(false);
        setChecking(false);
        if (kind == TxnKind::Rollback) Q_EMIT rollbackFinished(false);
        return;
    }
    m_txnConnectionName = QLatin1String(TXN_CONN_NAME);

    m_txnIface = new RpmOstreeTransaction(conn, this);
    connect(m_txnIface, &RpmOstreeTransaction::Message,
            this, &SoftwareUpdateBackend::onTxnMessage);
    connect(m_txnIface, &RpmOstreeTransaction::TaskBegin,
            this, &SoftwareUpdateBackend::onTxnTaskBegin);
    connect(m_txnIface, &RpmOstreeTransaction::TaskEnd,
            this, &SoftwareUpdateBackend::onTxnTaskEnd);
    connect(m_txnIface, &RpmOstreeTransaction::PercentProgress,
            this, &SoftwareUpdateBackend::onTxnProgress);
    connect(m_txnIface, &RpmOstreeTransaction::Finished,
            this, &SoftwareUpdateBackend::onTxnFinished);

    m_txnIface->start();
    if (kind != TxnKind::Check)
        openJobView(kind);
}

void SoftwareUpdateBackend::openJobView(TxnKind kind)
{
    closeJobView(QString());

    QString title;
    switch (kind) {
    case TxnKind::Rollback: title = i18n("Rolling back %1", m_osName); break;
    case TxnKind::Reset:    title = i18n("Removing layered packages"); break;
    case TxnKind::Check:    return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(JOBVIEW_SERVICE), QLatin1String(JOBVIEW_SERVER_PATH),
        QLatin1String(JOBVIEW_SERVER_IFACE), QStringLiteral("requestView"));
    // app name, icon name, capabilities (1 = killable)
    msg.setArguments({i18n("Software Update"),
                      QStringLiteral("system-software-update"), 1});

    auto *w = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, title](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError())
            return;
        const QString path = reply.value().path();
        if (!m_busy) {
            QDBusMessage t = QDBusMessage::createMethodCall(
                QLatin1String(JOBVIEW_SERVICE), path,
                QLatin1String(JOBVIEW_IFACE), QStringLiteral("terminate"));
            t.setArguments({QString()});
            QDBusConnection::sessionBus().asyncCall(t);
            return;
        }
        m_jobViewPath = path;
        jobViewCall(QStringLiteral("setInfoMessage"), {title});
        if (m_progressPercent > 0)
            jobViewCall(QStringLiteral("setPercent"),
                        {QVariant::fromValue(static_cast<quint32>(m_progressPercent))});
        QDBusConnection::sessionBus().connect(
            QString(), m_jobViewPath, QLatin1String(JOBVIEW_IFACE),
            QStringLiteral("cancelRequested"), this, SLOT(cancelUpgrade()));
    });
}

void SoftwareUpdateBackend::closeJobView(const QString &errorMessage)
{
    if (m_jobViewPath.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(
        QString(), m_jobViewPath, QLatin1String(JOBVIEW_IFACE),
        QStringLiteral("cancelRequested"), this, SLOT(cancelUpgrade()));
    jobViewCall(QStringLiteral("terminate"), {errorMessage});
    m_jobViewPath.clear();
}

void SoftwareUpdateBackend::jobViewCall(const QString &method, const QVariantList &args)
{
    if (m_jobViewPath.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(JOBVIEW_SERVICE), m_jobViewPath,
        QLatin1String(JOBVIEW_IFACE), method);
    msg.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(msg);
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
    jobViewCall(QStringLiteral("setDescriptionField"),
                {QVariant::fromValue(0u), QString(), msg});
}

void SoftwareUpdateBackend::onTxnTaskBegin(const QString &text)
{
    onTxnMessage(text);
}

void SoftwareUpdateBackend::onTxnTaskEnd(const QString &text)
{
    Q_UNUSED(text);
}

void SoftwareUpdateBackend::onTxnProgress(const QString &text, quint32 percent)
{
    if (!text.isEmpty())
        setProgressMessage(text);
    setHasPercent(true);
    setProgressPercent(static_cast<int>(percent));
    jobViewCall(QStringLiteral("setPercent"), {QVariant::fromValue(percent)});
}

void SoftwareUpdateBackend::onTxnFinished(bool success, const QString &errorMessage)
{
    const TxnKind kind = m_txnKind;
    const QString effectiveError =
        errorMessage.isEmpty() ? i18n("Unknown error") : errorMessage;
    // A failed check falls back to local state instead of the error view
    if (!success && kind != TxnKind::Check) {
        Q_EMIT errorOccurred(effectiveError);
    }
    closeJobView(success ? QString() : effectiveError);
    cleanupTransaction();
    setBusy(false);
    setResetting(false);

    switch (kind) {
    case TxnKind::Rollback:
        Q_EMIT rollbackFinished(success);
        break;
    case TxnKind::Reset:
        if (success)
            rebootSystem();
        break;
    case TxnKind::Check:
        fetchDeployments();
        break;
    }
}

// Rough, rounded wording so the estimate does not tick every second
static QString formatTimeRemaining(qint64 seconds)
{
    if (seconds < 0)
        return {};
    if (seconds < 60)
        return i18n("Less than a minute remaining");
    if (seconds < 3600) {
        const int minutes = static_cast<int>((seconds + 30) / 60);
        return i18np("About 1 minute remaining", "About %1 minutes remaining", minutes);
    }
    const qint64 roundedMs = (seconds + 150) / 300 * 300 * 1000;
    return i18n("About %1 remaining", KFormat().formatSpelloutDuration(roundedMs));
}

void SoftwareUpdateBackend::attachToHelper(const QVariantMap &state)
{
    m_helperUpgrade = true;
    setBusy(true);
    resetProgress();
    if (!state.value(QStringLiteral("osName")).toString().isEmpty())
        setOsName(state.value(QStringLiteral("osName")).toString());
    if (!state.value(QStringLiteral("version")).toString().isEmpty())
        setPendingVersion(state.value(QStringLiteral("version")).toString());
    m_progressLog = state.value(QStringLiteral("log")).toStringList();
    Q_EMIT progressLogReset();
    applyHelperState(state);
}

void SoftwareUpdateBackend::applyHelperState(const QVariantMap &state)
{
    const bool applying = state.value(QStringLiteral("applying")).toBool();
    setProgressMessage(state.value(QStringLiteral("message")).toString());
    setApplying(applying);
    setCancelling(state.value(QStringLiteral("cancelling")).toBool());
    setHasPercent(state.value(QStringLiteral("hasPercent")).toBool());
    setProgressPercent(state.value(QStringLiteral("percent")).toInt());

    const quint64 total = state.value(QStringLiteral("total")).toULongLong();
    const quint64 downloaded = state.value(QStringLiteral("downloaded")).toULongLong();
    if (total > 0)
        setDownloadSize(QLocale().formattedDataSize(static_cast<qint64>(total)));
    setDownloadedSize(!applying && downloaded > 0
        ? QLocale().formattedDataSize(static_cast<qint64>(downloaded))
        : QString());
    setTimeRemaining(formatTimeRemaining(state.value(QStringLiteral("eta")).toLongLong()));
}

void SoftwareUpdateBackend::onHelperStateChanged(const QVariantMap &state)
{
    const bool running = state.value(QStringLiteral("running")).toBool();
    if (m_helperUpgrade) {
        if (running)
            applyHelperState(state);
        return;
    }
    // Check if already running
    if (running && !m_busy) {
        setChecking(false);
        m_helperUpgrade = true;
        setBusy(true);
        resetProgress();
        applyHelperState(state);
    }
}

void SoftwareUpdateBackend::onHelperFinished(bool success, bool cancelled,
                                             const QString &errorMessage)
{
    if (!m_helperUpgrade)
        return;
    endHelperUpgrade();

    if (cancelled) {
        fetchDeployments();
        return;
    }
    if (!success)
        Q_EMIT errorOccurred(errorMessage.isEmpty() ? i18n("Unknown error") : errorMessage);
    else
        fetchDeployments();
    Q_EMIT upgradeFinished(success);
}

void SoftwareUpdateBackend::endHelperUpgrade()
{
    m_helperUpgrade = false;
    setBusy(false);
    setApplying(false);
    setCancelling(false);
    setDownloadedSize(QString());
    setTimeRemaining(QString());
}

void SoftwareUpdateBackend::setOsName(const QString &v)
{
    if (m_osName == v) return;
    m_osName = v;
    Q_EMIT osNameChanged();
}

/*  Fetch the identity repo's os-release and use its PRETTY_NAME directly,
 *  rather than guessing the future release's branding from the currently
 *  installed one
*/
void SoftwareUpdateBackend::fetchRemoteOsName()
{
    if (!m_netManager)
        m_netManager = new QNetworkAccessManager(this);

    const QUrl url = QUrl(QLatin1String(IDENTITY_OS_RELEASE_URL));
    auto *reply = m_netManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setOsName(readOsName());
            return;
        }
        QTextStream in(reply->readAll());
        const QString name = parseOsReleaseField(in, QStringLiteral("PRETTY_NAME"));
        setOsName(name.isEmpty() ? readOsName() : name);
    });
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
    if (!v.isEmpty()) {
        m_progressLog.append(v);
        Q_EMIT progressLogAppended(v);
    }
}

void SoftwareUpdateBackend::resetProgress()
{
    setProgressPercent(0);
    setHasPercent(false);
    setProgressMessage(QString());
    setDownloadedSize(QString());
    setTimeRemaining(QString());
    setApplying(false);
    setCancelling(false);
    m_progressLog.clear();
    Q_EMIT progressLogReset();
}

void SoftwareUpdateBackend::setHasPercent(bool v)
{
    if (m_hasPercent == v) return;
    m_hasPercent = v;
    Q_EMIT hasPercentChanged();
}

void SoftwareUpdateBackend::setDownloadSize(const QString &v)
{
    if (m_downloadSize == v) return;
    m_downloadSize = v;
    Q_EMIT downloadSizeChanged();
}

void SoftwareUpdateBackend::setDownloadedSize(const QString &v)
{
    if (m_downloadedSize == v) return;
    m_downloadedSize = v;
    Q_EMIT downloadedSizeChanged();
}

void SoftwareUpdateBackend::setTimeRemaining(const QString &v)
{
    if (m_timeRemaining == v) return;
    m_timeRemaining = v;
    Q_EMIT timeRemainingChanged();
}

void SoftwareUpdateBackend::setApplying(bool v)
{
    if (m_applying == v) return;
    m_applying = v;
    Q_EMIT applyingChanged();
}

void SoftwareUpdateBackend::setCancelling(bool v)
{
    if (m_cancelling == v) return;
    m_cancelling = v;
    Q_EMIT cancellingChanged();
}

void SoftwareUpdateBackend::setUpdateStaged(bool v)
{
    if (m_updateStaged == v) return;
    m_updateStaged = v;
    Q_EMIT updateStagedChanged();
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
    if (v)
        fetchChangelog();
    else
        setChangelogEntries({});
}

void SoftwareUpdateBackend::setBusy(bool v)
{
    if (m_busy == v) return;
    m_busy = v;
    Q_EMIT busyChanged();
}

void SoftwareUpdateBackend::setChecking(bool v)
{
    if (m_checking == v) return;
    m_checking = v;
    Q_EMIT checkingChanged();
}

void SoftwareUpdateBackend::setResetting(bool v)
{
    if (m_resetting == v) return;
    m_resetting = v;
    Q_EMIT resettingChanged();
}

void SoftwareUpdateBackend::setLayeredPackages(const QStringList &v)
{
    if (m_layeredPackages == v) return;
    m_layeredPackages = v;
    Q_EMIT layeredPackagesChanged();
}

void SoftwareUpdateBackend::setChangelogEntries(const QStringList &v)
{
    if (m_changelogEntries == v) return;
    m_changelogEntries = v;
    Q_EMIT changelogEntriesChanged();
}

/*  changelog.json maps language codes to a short list of bullet points
 *  describing the latest release, e.g. {"en": ["..."], "de": ["..."]}
*/
void SoftwareUpdateBackend::fetchChangelog()
{
    if (!m_netManager)
        m_netManager = new QNetworkAccessManager(this);

    const QUrl url(QStringLiteral("https://cdn.blossomos.org/changelog.json"));
    auto *reply = m_netManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;

        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QString lang = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
        QJsonArray entries = root.value(lang).toArray();
        if (entries.isEmpty())
            entries = root.value(QStringLiteral("en")).toArray();

        QStringList list;
        for (const auto &entry : std::as_const(entries))
            list << entry.toString();
        setChangelogEntries(list);
    });
}
