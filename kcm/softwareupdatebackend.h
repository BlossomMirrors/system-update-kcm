#pragma once
#include <QDBusAbstractInterface>
#include <QDBusConnection>
#include <QDBusPendingReply>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <qqmlintegration.h>

// Proxy for the rpm-ostree transaction peer connection
class RpmOstreeTransaction : public QDBusAbstractInterface
{
    Q_OBJECT
public:
    explicit RpmOstreeTransaction(const QDBusConnection &conn, QObject *parent = nullptr)
        : QDBusAbstractInterface(QString(),
                                 QStringLiteral("/"),
                                 "org.projectatomic.rpmostree1.Transaction",
                                 conn, parent)
    {}

    inline QDBusPendingReply<> start()  { return asyncCall(QStringLiteral("Start"));  }
    inline QDBusPendingReply<> cancel() { return asyncCall(QStringLiteral("Cancel")); }

Q_SIGNALS:
    void Message(const QString &message);
    void TaskBegin(const QString &text);
    void TaskEnd(const QString &text);
    void PercentProgress(const QString &text, quint32 percent);
    void Finished(bool success, const QString &errorMessage);
};

class SoftwareUpdateBackend : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString osName          READ osName          NOTIFY osNameChanged)
    Q_PROPERTY(QString currentVersion  READ currentVersion  NOTIFY currentVersionChanged)
    Q_PROPERTY(QString pendingVersion  READ pendingVersion  NOTIFY pendingVersionChanged)
    Q_PROPERTY(QString previousVersion READ previousVersion NOTIFY previousVersionChanged)
    Q_PROPERTY(int     progressPercent READ progressPercent NOTIFY progressPercentChanged)
    Q_PROPERTY(bool    hasPercent      READ hasPercent      NOTIFY hasPercentChanged)
    Q_PROPERTY(QString progressMessage READ progressMessage NOTIFY progressMessageChanged)
    Q_PROPERTY(QString downloadSize    READ downloadSize    NOTIFY downloadSizeChanged)
    Q_PROPERTY(QString downloadedSize  READ downloadedSize  NOTIFY downloadedSizeChanged)
    Q_PROPERTY(bool autoUpdateEnabled  READ autoUpdateEnabled NOTIFY autoUpdateEnabledChanged)
    Q_PROPERTY(bool updateAvailable    READ updateAvailable   NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool busy               READ busy              NOTIFY busyChanged)
    Q_PROPERTY(bool checking           READ checking          NOTIFY checkingChanged)
    Q_PROPERTY(bool resetting          READ resetting         NOTIFY resettingChanged)
    Q_PROPERTY(QStringList layeredPackages READ layeredPackages NOTIFY layeredPackagesChanged)
    Q_PROPERTY(QStringList changelogEntries READ changelogEntries NOTIFY changelogEntriesChanged)

public:
    explicit SoftwareUpdateBackend(QObject *parent = nullptr);
    ~SoftwareUpdateBackend() override;

    QString osName()          const { return m_osName; }
    QString currentVersion()  const { return m_currentVersion; }
    QString pendingVersion()  const { return m_pendingVersion; }
    QString previousVersion() const { return m_previousVersion; }
    int     progressPercent() const { return m_progressPercent; }
    bool    hasPercent()      const { return m_hasPercent; }
    QString progressMessage() const { return m_progressMessage; }
    QString downloadSize()    const { return m_downloadSize; }
    QString downloadedSize()  const { return m_downloadedSize; }
    bool autoUpdateEnabled()  const { return m_autoUpdateEnabled; }
    bool updateAvailable()    const { return m_updateAvailable; }
    bool busy()               const { return m_busy; }
    bool checking()           const { return m_checking; }
    bool resetting()          const { return m_resetting; }
    QStringList layeredPackages() const { return m_layeredPackages; }
    QStringList changelogEntries() const { return m_changelogEntries; }

    Q_INVOKABLE void startUpgrade();
    Q_INVOKABLE void scheduleUpgrade();
    Q_INVOKABLE void startRollback();
    Q_INVOKABLE void setAutoUpdate(bool enabled);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void rebootSystem();
    Q_INVOKABLE void startReset();

public Q_SLOTS:
    void cancelUpgrade();

Q_SIGNALS:
    void upgradeFinished(bool success);
    void rollbackFinished(bool success);
    void errorOccurred(const QString &message);

    void osNameChanged();
    void currentVersionChanged();
    void pendingVersionChanged();
    void previousVersionChanged();
    void progressPercentChanged();
    void hasPercentChanged();
    void progressMessageChanged();
    void downloadSizeChanged();
    void downloadedSizeChanged();
    void autoUpdateEnabledChanged();
    void updateAvailableChanged();
    void busyChanged();
    void checkingChanged();
    void resettingChanged();
    void layeredPackagesChanged();
    void changelogEntriesChanged();

private Q_SLOTS:
    void onTxnMessage(const QString &msg);
    void onTxnTaskBegin(const QString &text);
    void onTxnTaskEnd(const QString &text);
    void onTxnProgress(const QString &text, quint32 percent);
    void onTxnFinished(bool success, const QString &errorMessage);

private:
    enum class TxnKind { Upgrade, Rollback, Reset, Check };

    void setOsName(const QString &v);
    void fetchRemoteOsName(const QString &origin);
    void setCurrentVersion(const QString &v);
    void setPendingVersion(const QString &v);
    void setPreviousVersion(const QString &v);
    void setProgressPercent(int v);
    void setHasPercent(bool v);
    void setProgressMessage(const QString &v);
    void setDownloadSize(const QString &v);
    void setDownloadedSize(const QString &v);
    void startDiskProgress();
    void stopDiskProgress();
    void updateDownloadProgress();
    void finishDownloadPhase();
    void setAutoUpdateEnabled(bool v);
    void setUpdateAvailable(bool v);
    void setBusy(bool v);
    void setChecking(bool v);
    void setResetting(bool v);
    void setLayeredPackages(const QStringList &v);
    void setChangelogEntries(const QStringList &v);
    void fetchChangelog();

    void fetchDeployments();
    void fetchCachedUpdate();
    void readAutoUpdateState();
    void beginTransaction(const QString &address, TxnKind kind);
    void cleanupTransaction();
    void openJobView(TxnKind kind);
    void closeJobView(const QString &errorMessage);
    void jobViewCall(const QString &method, const QVariantList &args);

    QString m_osName;
    QString m_currentVersion;
    QString m_pendingVersion;
    QString m_previousVersion;
    int     m_progressPercent  = 0;
    bool    m_hasPercent       = false;
    QString m_progressMessage;
    QString m_downloadSize;
    QString m_downloadedSize;
    quint64 m_downloadBytes    = 0;
    quint64 m_pullTotalBytes   = 0;
    quint64 m_pullCompletedBytes = 0;
    quint64 m_taskBytes        = 0;
    quint64 m_taskBaseline     = 0;
    bool    m_taskActive       = false;
    quint64 m_lastProcessedBytes = 0;
    QElapsedTimer m_speedTimer;
    class QTimer *m_diskTimer  = nullptr;
    bool    m_autoUpdateEnabled = false;
    bool    m_updateAvailable   = false;
    bool    m_busy              = false;
    bool    m_checking          = false;
    bool    m_resetting         = false;
    QStringList m_layeredPackages;
    QStringList m_changelogEntries;
    class QNetworkAccessManager *m_netManager = nullptr;
    TxnKind m_txnKind           = TxnKind::Upgrade;

    QString              m_bootedChecksum;
    QString              m_txnConnectionName;
    QString              m_jobViewPath;
    RpmOstreeTransaction *m_txnIface = nullptr;
};
