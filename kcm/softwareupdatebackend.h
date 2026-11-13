#pragma once
#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <qqmlintegration.h>

class RpmOstreeTransaction;
class QDBusServiceWatcher;

class SoftwareUpdateBackend : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString osName          READ osName          NOTIFY osNameChanged)
    Q_PROPERTY(QString installedOsName READ installedOsName CONSTANT)
    Q_PROPERTY(QString currentVersion  READ currentVersion  NOTIFY currentVersionChanged)
    Q_PROPERTY(QString pendingVersion  READ pendingVersion  NOTIFY pendingVersionChanged)
    Q_PROPERTY(QString previousVersion READ previousVersion NOTIFY previousVersionChanged)
    Q_PROPERTY(int     progressPercent READ progressPercent NOTIFY progressPercentChanged)
    Q_PROPERTY(bool    hasPercent      READ hasPercent      NOTIFY hasPercentChanged)
    Q_PROPERTY(QString progressMessage READ progressMessage NOTIFY progressMessageChanged)
    Q_PROPERTY(QStringList progressLog READ progressLog     NOTIFY progressLogReset)
    Q_PROPERTY(QString downloadSize    READ downloadSize    NOTIFY downloadSizeChanged)
    Q_PROPERTY(QString downloadedSize  READ downloadedSize  NOTIFY downloadedSizeChanged)
    Q_PROPERTY(QString timeRemaining   READ timeRemaining   NOTIFY timeRemainingChanged)
    Q_PROPERTY(bool    applying        READ applying        NOTIFY applyingChanged)
    Q_PROPERTY(bool    cancelling      READ cancelling      NOTIFY cancellingChanged)
    Q_PROPERTY(bool autoUpdateEnabled  READ autoUpdateEnabled NOTIFY autoUpdateEnabledChanged)
    Q_PROPERTY(bool updateAvailable    READ updateAvailable   NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool updateStaged       READ updateStaged      NOTIFY updateStagedChanged)
    Q_PROPERTY(bool busy               READ busy              NOTIFY busyChanged)
    Q_PROPERTY(bool checking           READ checking          NOTIFY checkingChanged)
    Q_PROPERTY(bool resetting          READ resetting         NOTIFY resettingChanged)
    Q_PROPERTY(QStringList layeredPackages READ layeredPackages NOTIFY layeredPackagesChanged)
    Q_PROPERTY(QStringList changelogEntries READ changelogEntries NOTIFY changelogEntriesChanged)

public:
    explicit SoftwareUpdateBackend(QObject *parent = nullptr);
    ~SoftwareUpdateBackend() override;

    QString osName()          const { return m_osName; }
    QString installedOsName() const;
    QString currentVersion()  const { return m_currentVersion; }
    QString pendingVersion()  const { return m_pendingVersion; }
    QString previousVersion() const { return m_previousVersion; }
    int     progressPercent() const { return m_progressPercent; }
    bool    hasPercent()      const { return m_hasPercent; }
    QString progressMessage() const { return m_progressMessage; }
    QStringList progressLog() const { return m_progressLog; }
    QString downloadSize()    const { return m_downloadSize; }
    QString downloadedSize()  const { return m_downloadedSize; }
    QString timeRemaining()   const { return m_timeRemaining; }
    bool applying()           const { return m_applying; }
    bool cancelling()         const { return m_cancelling; }
    bool autoUpdateEnabled()  const { return m_autoUpdateEnabled; }
    bool updateAvailable()    const { return m_updateAvailable; }
    bool updateStaged()       const { return m_updateStaged; }
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
    Q_INVOKABLE void requestReboot();
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
    void progressLogReset();
    void progressLogAppended(const QString &line);
    void downloadSizeChanged();
    void downloadedSizeChanged();
    void timeRemainingChanged();
    void applyingChanged();
    void cancellingChanged();
    void autoUpdateEnabledChanged();
    void updateAvailableChanged();
    void updateStagedChanged();
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
    void onHelperStateChanged(const QVariantMap &state);
    void onHelperFinished(bool success, bool cancelled, const QString &errorMessage);

private:
    enum class TxnKind { Rollback, Reset, Check };

    void setOsName(const QString &v);
    void fetchRemoteOsName();
    void setCurrentVersion(const QString &v);
    void setPendingVersion(const QString &v);
    void setPreviousVersion(const QString &v);
    void setProgressPercent(int v);
    void setHasPercent(bool v);
    void setProgressMessage(const QString &v);
    void resetProgress();
    void setDownloadSize(const QString &v);
    void setDownloadedSize(const QString &v);
    void setTimeRemaining(const QString &v);
    void setApplying(bool v);
    void setCancelling(bool v);
    void setAutoUpdateEnabled(bool v);
    void setUpdateAvailable(bool v);
    void setUpdateStaged(bool v);
    void setBusy(bool v);
    void setChecking(bool v);
    void setResetting(bool v);
    void setLayeredPackages(const QStringList &v);
    void setChangelogEntries(const QStringList &v);
    void fetchChangelog();

    void runCheck();
    void attachToHelper(const QVariantMap &state);
    void applyHelperState(const QVariantMap &state);
    void endHelperUpgrade();
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
    QStringList m_progressLog;
    QString m_downloadSize;
    QString m_downloadedSize;
    QString m_timeRemaining;
    quint64 m_downloadBytes    = 0;
    bool    m_applying         = false;
    bool    m_cancelling       = false;
    bool    m_autoUpdateEnabled = false;
    bool    m_updateAvailable   = false;
    bool    m_updateStaged      = false;
    bool    m_busy              = false;
    bool    m_checking          = false;
    bool    m_resetting         = false;
    QStringList m_layeredPackages;
    QStringList m_changelogEntries;
    class QNetworkAccessManager *m_netManager = nullptr;
    TxnKind m_txnKind           = TxnKind::Check;

    // True while the KCM mirrors an upgrade owned by the update helper
    bool                 m_helperUpgrade = false;
    QDBusServiceWatcher *m_helperWatcher = nullptr;

    QString              m_bootedChecksum;
    QString              m_txnConnectionName;
    QString              m_jobViewPath;
    RpmOstreeTransaction *m_txnIface = nullptr;
};
