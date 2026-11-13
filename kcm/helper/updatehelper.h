#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

class RpmOstreeTransaction;

/*  Runs an rpm-ostree upgrade independently of the KCM. It stages the update
 *  (download and deploy, no reboot), mirrors its progress into a Plasma job
 *  notification, and publishes the same state on the session bus so any
 *  System Settings window can attach to it while it is running.
*/
class UpdateHelper : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.blossomos.SoftwareUpdateHelper")

public:
    explicit UpdateHelper(QObject *parent = nullptr);
    ~UpdateHelper() override;

public Q_SLOTS:
    Q_SCRIPTABLE void Upgrade(const QString &osName, const QString &version,
                              qulonglong expectedBytes);
    Q_SCRIPTABLE void Cancel();
    Q_SCRIPTABLE QVariantMap GetState() const;

Q_SIGNALS:
    Q_SCRIPTABLE void StateChanged(const QVariantMap &state);
    Q_SCRIPTABLE void Finished(bool success, bool cancelled, const QString &errorMessage);

private Q_SLOTS:
    void onTxnMessage(const QString &msg);
    void onTxnTaskBegin(const QString &text);
    void onTxnTaskEnd(const QString &text);
    void onTxnProgress(const QString &text, quint32 percent);
    void onTxnFinished(bool success, const QString &errorMessage);

private:
    QVariantMap state(bool withLog) const;
    void emitState();
    void setMessage(const QString &msg);
    void beginTransaction(const QString &address);
    void finish(bool success, const QString &errorMessage);
    void cleanupTransaction();

    void startDiskProgress();
    void stopDiskProgress();
    void updateDownloadProgress();
    void finishDownloadPhase();

    void openJobView();
    void closeJobView(const QString &errorMessage);
    void jobViewCall(const QString &method, const QVariantList &args);

    bool    m_running         = false;
    bool    m_applying        = false;
    bool    m_cancelRequested = false;
    int     m_percent         = 0;
    bool    m_hasPercent      = false;
    QString m_message;
    QStringList m_log;
    QString m_osName;
    QString m_version;
    quint64 m_generation      = 0;

    quint64 m_expectedBytes      = 0;
    quint64 m_pullTotalBytes     = 0;
    quint64 m_pullCompletedBytes = 0;
    quint64 m_taskBytes          = 0;
    quint64 m_taskBaseline       = 0;
    bool    m_taskActive         = false;
    quint64 m_downloadedBytes    = 0;
    double  m_speed              = 0;
    int     m_speedSamples       = 0;
    qint64  m_etaSeconds         = -1;
    QElapsedTimer m_speedTimer;
    QTimer  m_diskTimer;
    QTimer  m_quitTimer;
    QTimer  m_cancelTimer;
    class QDBusServiceWatcher *m_daemonWatcher = nullptr;

    QString m_txnConnectionName;
    QString m_jobViewPath;
    RpmOstreeTransaction *m_txnIface = nullptr;
};
