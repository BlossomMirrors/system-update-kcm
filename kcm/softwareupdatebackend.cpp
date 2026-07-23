#include "softwareupdatebackend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

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

// Pull org.opencontainers.image.version out of an OCI image config blob,
// as returned either by `skopeo inspect --config` or embedded locally in
// an rpm-ostree deployment's base-commit-meta
static QString extractImageVersionLabel(const QByteArray &configJson)
{
    const QJsonObject config =
        QJsonDocument::fromJson(configJson).object().value(QStringLiteral("config")).toObject();
    return config.value(QStringLiteral("Labels")).toObject()
                 .value(QStringLiteral("org.opencontainers.image.version")).toString();
}

// PRETTY_NAME always embeds the version as "Version: <ver>"; swap in a new
// version without hardcoding the surrounding branding text
static QString withImageVersion(const QString &prettyName, const QString &version)
{
    static const QRegularExpression re(QStringLiteral("Version:\\s*[^)\"]*"));
    if (!prettyName.contains(re))
        return prettyName;
    QString result = prettyName;
    result.replace(re, QStringLiteral("Version: ") + version);
    return result;
}

// Turn an rpm-ostree origin string (e.g. "ostree-unverified-registry:host/repo:tag"
// or "ostree-image-signed:docker://host/repo:tag") into a skopeo image reference
static QString toSkopeoReference(const QString &origin)
{
    const int colon = origin.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return {};
    const QString scheme = origin.left(colon);
    QString remainder = origin.mid(colon + 1);
    if (scheme == QLatin1String("ostree-remote-registry")) {
        const int second = remainder.indexOf(QLatin1Char(':'));
        if (second < 0)
            return {};
        remainder = remainder.mid(second + 1);
    }
    return remainder.startsWith(QLatin1String("docker://"))
        ? remainder : QLatin1String("docker://") + remainder;
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
    setBusy(true);
    setProgressPercent(0);
    setHasPercent(false);
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
    setHasPercent(false);
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
    setHasPercent(false);
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
        // The full OCI image config is embedded here too, so the version
        // label can be read without any network access for a staged deployment
        auto imageConfigJson = [&commitMeta](const QVariantMap &dep) -> QString {
            return commitMeta(dep).value(QStringLiteral("ostree.container.image-config")).toString();
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
        QString bootedDigest, pendingDigest, pendingImageConfig;
        QStringList layeredBooted, layeredPending;
        bool hasPending = false;
        quint64 bootedTs = 0;

        for (const auto &dep : std::as_const(deployments)) {
            if (dep.value(QStringLiteral("booted")).toBool()) {
                current = dep.value(QStringLiteral("version")).toString();
                bootedDigest = imageDigest(dep);
                bootedTs = dep.value(QStringLiteral("timestamp")).toULongLong();
                m_bootedChecksum = dep.value(QStringLiteral("checksum")).toString();
                layeredBooted = layeredOf(dep);
                break;
            }
        }

        for (const auto &dep : std::as_const(deployments)) {
            if (dep.value(QStringLiteral("booted")).toBool())
                continue;
            const QString version = dep.value(QStringLiteral("version")).toString();
            const bool staged     = dep.value(QStringLiteral("staged")).toBool();
            const quint64 ts      = dep.value(QStringLiteral("timestamp")).toULongLong();
            // Anything newer than the booted image counts as a pending update,
            // staged or not; older deployments are rollback targets
            if ((staged || ts > bootedTs) && pending.isEmpty()) {
                pending = version;
                pendingDigest = imageDigest(dep);
                pendingImageConfig = imageConfigJson(dep);
                layeredPending = layeredOf(dep);
                hasPending = true;
            } else if (!staged && ts <= bootedTs && previous.isEmpty()) {
                previous = version;
            }
        }

        const bool depUpdateAvailable = hasPending && pendingDigest != bootedDigest;
        setCurrentVersion(current);
        setPendingVersion(pending);
        setPreviousVersion(previous);
        // The pending deployment reflects what the next boot and future updates use
        setLayeredPackages(hasPending ? layeredPending : layeredBooted);
        setUpdateAvailable(depUpdateAvailable);
        if (depUpdateAvailable) {
            const QString version = extractImageVersionLabel(pendingImageConfig.toUtf8());
            setOsName(version.isEmpty() ? readOsName() : withImageVersion(readOsName(), version));
        }
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
        // The deployment list already resolved a stronger signal; do not
        // reissue the skopeo lookup or fall back to the local name
        const bool alreadyAvailable = m_updateAvailable;
        QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (!reply.isError()) {
            QVariantMap update;
            const QVariant v = reply.value().variant();
            if (v.canConvert<QDBusArgument>())
                qvariant_cast<QDBusArgument>(v) >> update;

            // Container images keep the same version label across builds,
            // so a new commit only shows up as a checksum or layer change
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
                    fetchRemoteOsName(update.value(QStringLiteral("origin")).toString());
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
        if (kind == TxnKind::Rollback)     Q_EMIT rollbackFinished(false);
        else if (kind == TxnKind::Upgrade) Q_EMIT upgradeFinished(false);
        return;
    }
    m_txnConnectionName = QLatin1String(TXN_CONN_NAME);

    m_txnIface = new RpmOstreeTransaction(conn, this);
    connect(m_txnIface, &RpmOstreeTransaction::Message,
            this, &SoftwareUpdateBackend::onTxnMessage);
    // Container layer downloads only report activity via task texts
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
    if (kind == TxnKind::Upgrade)
        startDiskProgress();
}

// Extract the last parenthesized human readable size from a daemon message
static quint64 parseSizeSuffix(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(
        "\\(([0-9]+(?:[.,][0-9]+)?)[\\s\\x{00a0}]*(B|kB|KB|KiB|MB|MiB|GB|GiB|TB|TiB)\\)"));
    QRegularExpressionMatch last;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        last = it.next();
    if (!last.hasMatch())
        return 0;
    QString num = last.captured(1);
    num.replace(QLatin1Char(','), QLatin1Char('.'));
    const QString unit = last.captured(2);
    double mult = 1;
    if (unit == QLatin1String("kB") || unit == QLatin1String("KB")) mult = 1e3;
    else if (unit == QLatin1String("KiB")) mult = 1024.0;
    else if (unit == QLatin1String("MB"))  mult = 1e6;
    else if (unit == QLatin1String("MiB")) mult = 1024.0 * 1024;
    else if (unit == QLatin1String("GB"))  mult = 1e9;
    else if (unit == QLatin1String("GiB")) mult = 1024.0 * 1024 * 1024;
    else if (unit == QLatin1String("TB"))  mult = 1e12;
    else if (unit == QLatin1String("TiB")) mult = 1024.0 * 1024 * 1024 * 1024;
    return static_cast<quint64>(num.toDouble() * mult);
}

// Total received bytes over all physical network interfaces
static quint64 totalRxBytes()
{
    QFile f(QStringLiteral("/proc/net/dev"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    QTextStream in(&f);
    in.readLine();
    in.readLine();
    quint64 total = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        if (line.left(colon).trimmed() == QLatin1String("lo"))
            continue;
        const QStringList fields =
            line.mid(colon + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (!fields.isEmpty())
            total += fields.at(0).toULongLong();
    }
    return total;
}

// rpm-ostree reports no byte progress while pulling container layers.
// The daemon announces exact task sizes though, so completed tasks are
// counted precisely and only the running task is interpolated from
// network receive counters, clamped to that task's size.
void SoftwareUpdateBackend::startDiskProgress()
{
    m_pullTotalBytes = 0;
    m_pullCompletedBytes = 0;
    m_taskActive = false;
    m_lastProcessedBytes = 0;
    m_speedTimer.start();

    if (!m_diskTimer) {
        m_diskTimer = new QTimer(this);
        m_diskTimer->setInterval(1000);
        connect(m_diskTimer, &QTimer::timeout,
                this, &SoftwareUpdateBackend::updateDownloadProgress);
    }
    m_diskTimer->start();
}

void SoftwareUpdateBackend::updateDownloadProgress()
{
    const quint64 total = m_pullTotalBytes > 0 ? m_pullTotalBytes : m_downloadBytes;
    if (total == 0)
        return;

    quint64 current = m_pullCompletedBytes;
    if (m_taskActive) {
        const quint64 rx = totalRxBytes();
        const quint64 delta = rx > m_taskBaseline ? rx - m_taskBaseline : 0;
        current += qMin(delta, m_taskBytes);
    }
    current = qMin(current, total);
    if (current == 0)
        return;

    const qint64 elapsedMs = m_speedTimer.restart();
    const quint64 bytesDelta =
        current > m_lastProcessedBytes ? current - m_lastProcessedBytes : 0;
    m_lastProcessedBytes = current;
    if (elapsedMs > 0)
        jobViewCall(QStringLiteral("setSpeed"),
                    {QVariant::fromValue(bytesDelta * 1000 / static_cast<quint64>(elapsedMs))});

    const int pct = static_cast<int>(qMin<quint64>(99, current * 100 / total));
    setHasPercent(true);
    setDownloadedSize(QLocale().formattedDataSize(static_cast<qint64>(current)));
    jobViewCall(QStringLiteral("setProcessedAmount"),
                {QVariant::fromValue(current), QStringLiteral("bytes")});
    if (pct > m_progressPercent) {
        setProgressPercent(pct);
        jobViewCall(QStringLiteral("setPercent"),
                    {QVariant::fromValue(static_cast<quint32>(pct))});
    }
}

void SoftwareUpdateBackend::stopDiskProgress()
{
    if (m_diskTimer)
        m_diskTimer->stop();
    setDownloadedSize(QString());
}

void SoftwareUpdateBackend::openJobView(TxnKind kind)
{
    closeJobView(QString());

    QString title;
    switch (kind) {
    case TxnKind::Rollback: title = i18n("Rolling back %1", m_osName); break;
    case TxnKind::Reset:    title = i18n("Removing layered packages"); break;
    case TxnKind::Upgrade:  title = i18n("Updating %1", m_osName); break;
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
        if (m_downloadBytes > 0)
            jobViewCall(QStringLiteral("setTotalAmount"),
                        {QVariant::fromValue(m_downloadBytes), QStringLiteral("bytes")});
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
    stopDiskProgress();
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

    // The daemon announces the exact remaining download at pull start
    if (msg.contains(QLatin1String("needed:"))) {
        const quint64 bytes = parseSizeSuffix(msg);
        if (bytes > 0) {
            m_pullTotalBytes += bytes;
            m_downloadBytes = m_pullTotalBytes;
            setDownloadSize(QLocale().formattedDataSize(
                static_cast<qint64>(m_pullTotalBytes)));
            jobViewCall(QStringLiteral("setTotalAmount"),
                        {QVariant::fromValue(m_pullTotalBytes), QStringLiteral("bytes")});
        }
    }
}

void SoftwareUpdateBackend::onTxnTaskBegin(const QString &text)
{
    setProgressMessage(text);
    jobViewCall(QStringLiteral("setDescriptionField"),
                {QVariant::fromValue(0u), QString(), text});

    const quint64 bytes = parseSizeSuffix(text);
    if (bytes > 0) {
        m_taskBytes = bytes;
        m_taskBaseline = totalRxBytes();
        m_taskActive = true;
    } else if (m_pullCompletedBytes > 0) {
        // A sizeless task after fetching means staging has begun
        finishDownloadPhase();
    }
}

void SoftwareUpdateBackend::onTxnTaskEnd(const QString &text)
{
    Q_UNUSED(text);
    if (m_taskActive) {
        m_taskActive = false;
        m_pullCompletedBytes += m_taskBytes;
        updateDownloadProgress();
    }
    // Parsed sizes are rounded, so treat within two percent as done
    if (m_pullTotalBytes > 0
        && m_pullCompletedBytes + m_pullTotalBytes / 50 >= m_pullTotalBytes)
        finishDownloadPhase();
}

// Staging writes cannot be measured, so fall back to an indeterminate bar
void SoftwareUpdateBackend::finishDownloadPhase()
{
    if (!m_diskTimer || !m_diskTimer->isActive())
        return;
    stopDiskProgress();
    setHasPercent(false);
    setProgressPercent(0);
}

void SoftwareUpdateBackend::onTxnProgress(const QString &text, quint32 percent)
{
    if (!text.isEmpty())
        setProgressMessage(text);
    // Disk based download progress wins over the jumpy per phase percents
    if (m_diskTimer && m_diskTimer->isActive())
        return;
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
        if (success)
            fetchDeployments();
        else
            setChecking(false);
        break;
    case TxnKind::Upgrade:
        if (success)
            fetchDeployments();
        Q_EMIT upgradeFinished(success);
        break;
    }
}

void SoftwareUpdateBackend::setOsName(const QString &v)
{
    if (m_osName == v) return;
    m_osName = v;
    Q_EMIT osNameChanged();
}

// No local metadata is available yet for a merely cached (not staged) update,
// so read just the image's manifest and config over the registry API. This is
// a couple of small JSON documents, not a pull of the (multi-gigabyte) image.
void SoftwareUpdateBackend::fetchRemoteOsName(const QString &origin)
{
    const QString ref = toSkopeoReference(origin);
    if (ref.isEmpty()) {
        setOsName(readOsName());
        return;
    }

    auto *proc = new QProcess(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus status) {
        proc->deleteLater();
        QString version;
        if (exitCode == 0 && status == QProcess::NormalExit)
            version = extractImageVersionLabel(proc->readAllStandardOutput());
        setOsName(version.isEmpty() ? readOsName() : withImageVersion(readOsName(), version));
    });
    proc->start(QStringLiteral("skopeo"),
                {QStringLiteral("inspect"), QStringLiteral("--config"), ref});
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

// changelog.json maps language codes to a short list of bullet points
// describing the latest release, e.g. {"en": ["..."], "de": ["..."]}
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
