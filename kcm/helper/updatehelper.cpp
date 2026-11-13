#include "updatehelper.h"
#include "../rpmostreetransaction.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <KLocalizedString>

static constexpr auto RPMOSTREE_SERVICE   = "org.projectatomic.rpmostree1";
static constexpr auto RPMOSTREE_OS_PATH   = "/org/projectatomic/rpmostree1/default";
static constexpr auto RPMOSTREE_OS_IFACE  = "org.projectatomic.rpmostree1.OS";
static constexpr auto TXN_CONN_NAME       = "kcm-helper-rpmostree-txn";
static constexpr auto ABANDON_CONN_NAME   = "kcm-helper-rpmostree-abandoned";
static constexpr auto JOBVIEW_SERVICE      = "org.kde.JobViewServer";
static constexpr auto JOBVIEW_SERVER_PATH  = "/JobViewServer";
static constexpr auto JOBVIEW_SERVER_IFACE = "org.kde.JobViewServer";
static constexpr auto JOBVIEW_IFACE        = "org.kde.JobViewV2";

// Leave without an upgrade request, or once a finished upgrade was reported
static constexpr int IDLE_QUIT_MS = 30 * 1000;
// Give up waiting for the daemon to confirm a cancellation
static constexpr int CANCEL_TIMEOUT_MS = 20 * 1000;
// The Upgrade reply waits for the Polkit prompt, which can take a while
static constexpr int POLKIT_TIMEOUT_MS = 10 * 60 * 1000;
// Speed samples needed before a remaining time estimate is trusted
static constexpr int MIN_SPEED_SAMPLES = 3;

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

/*  A transaction whose requester is gone still has to be started to be
 *  cancelled, otherwise the daemon keeps it around as the active one
*/
static void abandonTransaction(const QString &address)
{
    const QString name = QLatin1String(ABANDON_CONN_NAME);
    QDBusConnection conn = QDBusConnection::connectToPeer(address, name);
    if (!conn.isConnected()) {
        QDBusConnection::disconnectFromPeer(name);
        return;
    }
    auto *txn = new RpmOstreeTransaction(conn);
    auto done = [txn, name] {
        txn->deleteLater();
        QDBusConnection::disconnectFromPeer(name);
    };
    QObject::connect(txn, &RpmOstreeTransaction::Finished, txn, done);
    QTimer::singleShot(CANCEL_TIMEOUT_MS, txn, done);
    txn->cancel();
    txn->start();
}

UpdateHelper::UpdateHelper(QObject *parent)
    : QObject(parent)
{
    m_diskTimer.setInterval(1000);
    connect(&m_diskTimer, &QTimer::timeout, this, &UpdateHelper::updateDownloadProgress);

    m_quitTimer.setSingleShot(true);
    m_quitTimer.setInterval(IDLE_QUIT_MS);
    connect(&m_quitTimer, &QTimer::timeout, qApp, &QCoreApplication::quit);
    m_quitTimer.start();

    m_cancelTimer.setSingleShot(true);
    m_cancelTimer.setInterval(CANCEL_TIMEOUT_MS);
    connect(&m_cancelTimer, &QTimer::timeout, this, [this] {
        if (m_running)
            finish(false, QString());
    });

    // Finished never arrives when the daemon goes away mid transaction
    m_daemonWatcher = new QDBusServiceWatcher(
        QLatin1String(RPMOSTREE_SERVICE), QDBusConnection::systemBus(),
        QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_daemonWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        if (m_running && m_txnIface)
            finish(false, i18n("The update service stopped unexpectedly"));
    });
}

UpdateHelper::~UpdateHelper()
{
    closeJobView(QString());
    cleanupTransaction();
}

void UpdateHelper::Upgrade(const QString &osName, const QString &version,
                           qulonglong expectedBytes)
{
    if (m_running)
        return;

    m_quitTimer.stop();
    m_running = true;
    m_applying = false;
    m_cancelRequested = false;
    m_percent = 0;
    m_hasPercent = false;
    m_message.clear();
    m_log.clear();
    m_osName = osName;
    m_version = version;
    m_expectedBytes = expectedBytes;
    m_downloadedBytes = 0;
    m_etaSeconds = -1;
    const quint64 generation = ++m_generation;
    emitState();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(RPMOSTREE_SERVICE), QLatin1String(RPMOSTREE_OS_PATH),
        QLatin1String(RPMOSTREE_OS_IFACE), QStringLiteral("Upgrade"));
    msg.setArguments({QVariant::fromValue(QVariantMap{})});
    msg.setInteractiveAuthorizationAllowed(true);

    auto *w = new QDBusPendingCallWatcher(
        QDBusConnection::systemBus().asyncCall(msg, POLKIT_TIMEOUT_MS), this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<QString> reply = *watcher;
        const bool stale = generation != m_generation || !m_running;
        if (reply.isError()) {
            if (!stale)
                finish(false, reply.error().message());
            return;
        }
        if (stale) {
            abandonTransaction(reply.value());
            return;
        }
        beginTransaction(reply.value());
    });
}

void UpdateHelper::Cancel()
{
    if (!m_running || m_cancelRequested)
        return;
    m_cancelRequested = true;
    setMessage(i18n("Cancelling…"));
    // Without a transaction yet, the cancel is sent once its address arrives
    if (m_txnIface)
        m_txnIface->cancel();
    m_cancelTimer.start();
}

QVariantMap UpdateHelper::GetState() const
{
    return state(true);
}

QVariantMap UpdateHelper::state(bool withLog) const
{
    QVariantMap s{
        {QStringLiteral("running"),    m_running},
        {QStringLiteral("applying"),   m_applying},
        {QStringLiteral("cancelling"), m_cancelRequested},
        {QStringLiteral("percent"),    m_percent},
        {QStringLiteral("hasPercent"), m_hasPercent},
        {QStringLiteral("message"),    m_message},
        {QStringLiteral("downloaded"), QVariant::fromValue<qulonglong>(m_downloadedBytes)},
        {QStringLiteral("total"),      QVariant::fromValue<qulonglong>(
                                           m_pullTotalBytes > 0 ? m_pullTotalBytes
                                                                : m_expectedBytes)},
        {QStringLiteral("eta"),        QVariant::fromValue<qlonglong>(m_etaSeconds)},
        {QStringLiteral("osName"),     m_osName},
        {QStringLiteral("version"),    m_version},
    };
    if (withLog)
        s.insert(QStringLiteral("log"), m_log);
    return s;
}

void UpdateHelper::emitState()
{
    Q_EMIT StateChanged(state(false));
}

void UpdateHelper::setMessage(const QString &msg)
{
    if (msg.isEmpty() || msg == m_message)
        return;
    m_message = msg;
    m_log.append(msg);
    jobViewCall(QStringLiteral("setDescriptionField"),
                {QVariant::fromValue(0u), QString(), msg});
    emitState();
}

void UpdateHelper::beginTransaction(const QString &address)
{
    cleanupTransaction();

    QDBusConnection conn =
        QDBusConnection::connectToPeer(address, QLatin1String(TXN_CONN_NAME));
    m_txnConnectionName = QLatin1String(TXN_CONN_NAME);
    if (!conn.isConnected()) {
        finish(false, i18n("Failed to connect to rpm-ostree transaction"));
        return;
    }

    m_txnIface = new RpmOstreeTransaction(conn, this);
    connect(m_txnIface, &RpmOstreeTransaction::Message,
            this, &UpdateHelper::onTxnMessage);
    // Container layer downloads only report activity via task texts
    connect(m_txnIface, &RpmOstreeTransaction::TaskBegin,
            this, &UpdateHelper::onTxnTaskBegin);
    connect(m_txnIface, &RpmOstreeTransaction::TaskEnd,
            this, &UpdateHelper::onTxnTaskEnd);
    connect(m_txnIface, &RpmOstreeTransaction::PercentProgress,
            this, &UpdateHelper::onTxnProgress);
    connect(m_txnIface, &RpmOstreeTransaction::Finished,
            this, &UpdateHelper::onTxnFinished);

    m_txnIface->start();
    if (m_cancelRequested) {
        m_txnIface->cancel();
        return;
    }
    openJobView();
    startDiskProgress();
}

void UpdateHelper::finish(bool success, const QString &errorMessage)
{
    const bool cancelled = m_cancelRequested;
    const QString effectiveError = success || cancelled ? QString()
        : errorMessage.isEmpty() ? i18n("Unknown error") : errorMessage;

    m_cancelTimer.stop();
    closeJobView(cancelled ? i18n("The update was cancelled") : effectiveError);
    cleanupTransaction();
    m_running = false;
    m_applying = false;
    m_cancelRequested = false;
    m_etaSeconds = -1;
    emitState();
    Q_EMIT Finished(success && !cancelled, cancelled, effectiveError);
    m_quitTimer.start();
}

void UpdateHelper::cleanupTransaction()
{
    stopDiskProgress();
    delete m_txnIface;
    m_txnIface = nullptr;

    if (!m_txnConnectionName.isEmpty()) {
        QDBusConnection::disconnectFromPeer(m_txnConnectionName);
        m_txnConnectionName.clear();
    }
}

/*  rpm-ostree reports no byte progress while pulling container layers.
 *  The daemon announces exact task sizes though, so completed tasks are
 *  counted precisely and only the running task is interpolated from
 *  network receive counters, clamped to that task's size.
*/
void UpdateHelper::startDiskProgress()
{
    m_pullTotalBytes = 0;
    m_pullCompletedBytes = 0;
    m_taskActive = false;
    m_downloadedBytes = 0;
    m_speed = 0;
    m_speedSamples = 0;
    m_etaSeconds = -1;
    m_speedTimer.start();
    m_diskTimer.start();
}

void UpdateHelper::stopDiskProgress()
{
    m_diskTimer.stop();
}

void UpdateHelper::updateDownloadProgress()
{
    const quint64 total = m_pullTotalBytes > 0 ? m_pullTotalBytes : m_expectedBytes;
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
        current > m_downloadedBytes ? current - m_downloadedBytes : 0;
    m_downloadedBytes = current;
    if (elapsedMs > 0) {
        // Smooth over roughly the last few seconds so the estimate stays calm
        const double instant = bytesDelta * 1000.0 / static_cast<double>(elapsedMs);
        m_speed = m_speedSamples == 0 ? instant : 0.8 * m_speed + 0.2 * instant;
        ++m_speedSamples;
        jobViewCall(QStringLiteral("setSpeed"),
                    {QVariant::fromValue(static_cast<quint64>(m_speed))});
    }
    m_etaSeconds = m_speedSamples >= MIN_SPEED_SAMPLES && m_speed >= 1.0
        ? static_cast<qint64>(static_cast<double>(total - current) / m_speed)
        : -1;

    const int pct = static_cast<int>(qMin<quint64>(99, current * 100 / total));
    m_hasPercent = true;
    jobViewCall(QStringLiteral("setProcessedAmount"),
                {QVariant::fromValue(current), QStringLiteral("bytes")});
    if (pct > m_percent) {
        m_percent = pct;
        jobViewCall(QStringLiteral("setPercent"),
                    {QVariant::fromValue(static_cast<quint32>(pct))});
    }
    emitState();
}

// Staging writes cannot be measured, so fall back to the daemon's own percentages or an indeterminate bar
void UpdateHelper::finishDownloadPhase()
{
    if (m_applying)
        return;
    stopDiskProgress();
    m_applying = true;
    m_hasPercent = false;
    m_percent = 0;
    m_etaSeconds = -1;
    jobViewCall(QStringLiteral("setInfoMessage"), {i18n("Installing %1", m_osName)});
    jobViewCall(QStringLiteral("setSpeed"), {QVariant::fromValue(quint64(0))});
    emitState();
}

void UpdateHelper::onTxnMessage(const QString &msg)
{
    setMessage(msg);

    // The daemon announces the exact remaining download at pull start
    if (msg.contains(QLatin1String("needed:"))) {
        const quint64 bytes = parseSizeSuffix(msg);
        if (bytes > 0) {
            m_pullTotalBytes += bytes;
            jobViewCall(QStringLiteral("setTotalAmount"),
                        {QVariant::fromValue(m_pullTotalBytes), QStringLiteral("bytes")});
            emitState();
        }
    }
}

void UpdateHelper::onTxnTaskBegin(const QString &text)
{
    setMessage(text);

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

void UpdateHelper::onTxnTaskEnd(const QString &text)
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

void UpdateHelper::onTxnProgress(const QString &text, quint32 percent)
{
    setMessage(text);
    // Byte based download progress wins over the jumpy per phase percents
    if (!m_applying && m_pullTotalBytes > 0)
        return;
    m_hasPercent = true;
    m_percent = static_cast<int>(percent);
    jobViewCall(QStringLiteral("setPercent"), {QVariant::fromValue(percent)});
    emitState();
}

void UpdateHelper::onTxnFinished(bool success, const QString &errorMessage)
{
    finish(success, errorMessage);
}

void UpdateHelper::openJobView()
{
    closeJobView(QString());

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(JOBVIEW_SERVICE), QLatin1String(JOBVIEW_SERVER_PATH),
        QLatin1String(JOBVIEW_SERVER_IFACE), QStringLiteral("requestView"));
    // app name, icon name, capabilities (1 = killable)
    msg.setArguments({i18n("Software Update"),
                      QStringLiteral("system-software-update"), 1});

    const quint64 generation = m_generation;
    auto *w = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError())
            return;
        const QString path = reply.value().path();
        if (!m_running || generation != m_generation) {
            QDBusMessage t = QDBusMessage::createMethodCall(
                QLatin1String(JOBVIEW_SERVICE), path,
                QLatin1String(JOBVIEW_IFACE), QStringLiteral("terminate"));
            t.setArguments({QString()});
            QDBusConnection::sessionBus().asyncCall(t);
            return;
        }
        m_jobViewPath = path;
        jobViewCall(QStringLiteral("setInfoMessage"),
                    {m_applying ? i18n("Installing %1", m_osName)
                                : i18n("Downloading %1", m_osName)});
        if (!m_message.isEmpty())
            jobViewCall(QStringLiteral("setDescriptionField"),
                        {QVariant::fromValue(0u), QString(), m_message});
        const quint64 total = m_pullTotalBytes > 0 ? m_pullTotalBytes : m_expectedBytes;
        if (total > 0)
            jobViewCall(QStringLiteral("setTotalAmount"),
                        {QVariant::fromValue(total), QStringLiteral("bytes")});
        if (m_percent > 0)
            jobViewCall(QStringLiteral("setPercent"),
                        {QVariant::fromValue(static_cast<quint32>(m_percent))});
        QDBusConnection::sessionBus().connect(
            QString(), m_jobViewPath, QLatin1String(JOBVIEW_IFACE),
            QStringLiteral("cancelRequested"), this, SLOT(Cancel()));
    });
}

void UpdateHelper::closeJobView(const QString &errorMessage)
{
    if (m_jobViewPath.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(
        QString(), m_jobViewPath, QLatin1String(JOBVIEW_IFACE),
        QStringLiteral("cancelRequested"), this, SLOT(Cancel()));
    jobViewCall(QStringLiteral("terminate"), {errorMessage});
    m_jobViewPath.clear();
}

void UpdateHelper::jobViewCall(const QString &method, const QVariantList &args)
{
    if (m_jobViewPath.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QLatin1String(JOBVIEW_SERVICE), m_jobViewPath,
        QLatin1String(JOBVIEW_IFACE), method);
    msg.setArguments(args);
    QDBusConnection::sessionBus().asyncCall(msg);
}
