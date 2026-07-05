#pragma once
#include <QDBusAbstractInterface>
#include <QDBusConnection>
#include <QDBusPendingReply>
#include <QObject>
#include <QString>
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
    Q_PROPERTY(QString progressMessage READ progressMessage NOTIFY progressMessageChanged)
    Q_PROPERTY(bool autoUpdateEnabled  READ autoUpdateEnabled NOTIFY autoUpdateEnabledChanged)
    Q_PROPERTY(bool updateAvailable    READ updateAvailable   NOTIFY updateAvailableChanged)
    Q_PROPERTY(bool busy               READ busy              NOTIFY busyChanged)
    Q_PROPERTY(bool checking           READ checking          NOTIFY checkingChanged)

public:
    explicit SoftwareUpdateBackend(QObject *parent = nullptr);
    ~SoftwareUpdateBackend() override;

    QString osName()          const { return m_osName; }
    QString currentVersion()  const { return m_currentVersion; }
    QString pendingVersion()  const { return m_pendingVersion; }
    QString previousVersion() const { return m_previousVersion; }
    int     progressPercent() const { return m_progressPercent; }
    QString progressMessage() const { return m_progressMessage; }
    bool autoUpdateEnabled()  const { return m_autoUpdateEnabled; }
    bool updateAvailable()    const { return m_updateAvailable; }
    bool busy()               const { return m_busy; }
    bool checking()           const { return m_checking; }

    Q_INVOKABLE void startUpgrade();
    Q_INVOKABLE void scheduleUpgrade();
    Q_INVOKABLE void cancelUpgrade();
    Q_INVOKABLE void startRollback();
    Q_INVOKABLE void setAutoUpdate(bool enabled);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void rebootSystem();

Q_SIGNALS:
    void upgradeFinished(bool success);
    void rollbackFinished(bool success);
    void errorOccurred(const QString &message);

    void osNameChanged();
    void currentVersionChanged();
    void pendingVersionChanged();
    void previousVersionChanged();
    void progressPercentChanged();
    void progressMessageChanged();
    void autoUpdateEnabledChanged();
    void updateAvailableChanged();
    void busyChanged();
    void checkingChanged();

private Q_SLOTS:
    void onTxnMessage(const QString &msg);
    void onTxnProgress(const QString &text, quint32 percent);
    void onTxnFinished(bool success, const QString &errorMessage);

private:
    void setCurrentVersion(const QString &v);
    void setPendingVersion(const QString &v);
    void setPreviousVersion(const QString &v);
    void setProgressPercent(int v);
    void setProgressMessage(const QString &v);
    void setAutoUpdateEnabled(bool v);
    void setUpdateAvailable(bool v);
    void setBusy(bool v);
    void setChecking(bool v);

    void fetchDeployments();
    void readAutoUpdateState();
    void beginTransaction(const QString &address, bool isRollback);
    void cleanupTransaction();

    QString m_osName;
    QString m_currentVersion;
    QString m_pendingVersion;
    QString m_previousVersion;
    int     m_progressPercent  = 0;
    QString m_progressMessage;
    bool    m_autoUpdateEnabled = false;
    bool    m_updateAvailable   = false;
    bool    m_busy              = false;
    bool    m_checking          = false;
    bool    m_txnIsRollback     = false;

    QString              m_txnConnectionName;
    RpmOstreeTransaction *m_txnIface = nullptr;
};
