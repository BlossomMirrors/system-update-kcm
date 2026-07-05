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
static constexpr auto JOBVIEW_SERVICE      = "org.kde.JobViewServer";
static constexpr auto JOBVIEW_SERVER_PATH  = "/JobViewServer";
static constexpr auto JOBVIEW_SERVER_IFACE = "org.kde.JobViewServer";
static constexpr auto JOBVIEW_IFACE        = "org.kde.JobViewV2";

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
    closeJobView(QString());
    cleanupTransaction();
}

void SoftwareUpdateBackend::checkForUpdates()
{
    setChecking(true);
    fetchDeployments();
    readAutoUpdateState();
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

void SoftwareUpdateBackend::startUpgrade()
{
    if (m_busy) return;
    setBusy(true);
    setProgressPercent(0);
    setProgressMessage(QString());

    auto *w = new QDBusPendingCallWatcher(
        authCall(RPMOSTREE_SERVICE, RPMOSTREE_OS_PATH, RPMOSTREE_OS_IFACE,
                 QStringLiteral("Upgrade"), {QVariant::fromValue(QVariantMap{})}), this);

    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT errorOccurred(reply.error().message());
            setBusy(false);
            Q_EMIT upgradeFinished(false);
            return;
        }
        beginTransaction(reply.value(), TxnKind::Upgrade);
    });
}

void SoftwareUpdateBackend::startRollback()
{
    if (m_busy) return;
    setBusy(true);
    setProgressPercent(0);
    setProgressMessage(QString());

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
    setProgressPercent(0);
    setProgressMessage(QString());

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
                Q_EMIT errorOccurred(watcher->error().message());
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
                Q_EMIT errorOccurred(watcher->error().message());
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

        // Extract the container image digest from base-commit-meta (nested a{sv}).
        auto imageDigest = [](const QVariantMap &dep) -> QString {
            const QVariant v = dep.value(QStringLiteral("base-commit-meta"));
            if (!v.canConvert<QDBusArgument>()) return {};
            QVariantMap meta;
            qvariant_cast<QDBusArgument>(v) >> meta;
            return meta.value(QStringLiteral("ostree.manifest-digest")).toString();
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
        QString bootedDigest, pendingDigest;
        QStringList layeredBooted, layeredStaged;
        bool hasStaged = false;
        for (const auto &dep : std::as_const(deployments)) {
            const QString version = dep.value(QStringLiteral("version")).toString();
            const bool booted     = dep.value(QStringLiteral("booted")).toBool();
            const bool staged     = dep.value(QStringLiteral("staged")).toBool();
            if (booted && current.isEmpty()) {
                current = version;
                bootedDigest = imageDigest(dep);
                layeredBooted = layeredOf(dep);
            } else if (staged && pending.isEmpty()) {
                pending = version;
                pendingDigest = imageDigest(dep);
                layeredStaged = layeredOf(dep);
                hasStaged = true;
            } else if (!booted && !staged && previous.isEmpty()) {
                previous = version;
            }
        }
        setCurrentVersion(current);
        setPendingVersion(pending);
        setPreviousVersion(previous);
        // The staged deployment reflects what the next boot and future updates use
        setLayeredPackages(hasStaged ? layeredStaged : layeredBooted);
        setUpdateAvailable(!pending.isEmpty() && pendingDigest != bootedDigest);
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
        setAutoUpdateEnabled(enabled);
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
        if (kind == TxnKind::Rollback)     Q_EMIT rollbackFinished(false);
        else if (kind == TxnKind::Upgrade) Q_EMIT upgradeFinished(false);
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
    openJobView(kind);
}

void SoftwareUpdateBackend::openJobView(TxnKind kind)
{
    closeJobView(QString());

    QString title;
    switch (kind) {
    case TxnKind::Rollback: title = i18n("Rolling back %1", m_osName); break;
    case TxnKind::Reset:    title = i18n("Removing layered packages"); break;
    case TxnKind::Upgrade:  title = i18n("Updating %1", m_osName); break;
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

void SoftwareUpdateBackend::onTxnProgress(const QString &text, quint32 percent)
{
    if (!text.isEmpty())
        setProgressMessage(text);
    setProgressPercent(static_cast<int>(percent));
    jobViewCall(QStringLiteral("setPercent"), {QVariant::fromValue(percent)});
}

void SoftwareUpdateBackend::onTxnFinished(bool success, const QString &errorMessage)
{
    const QString effectiveError =
        errorMessage.isEmpty() ? i18n("Unknown error") : errorMessage;
    if (!success) {
        Q_EMIT errorOccurred(effectiveError);
    }
    closeJobView(success ? QString() : effectiveError);

    const TxnKind kind = m_txnKind;
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
    case TxnKind::Upgrade:
        if (success)
            fetchDeployments();
        Q_EMIT upgradeFinished(success);
        break;
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
