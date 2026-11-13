#pragma once
#include <QDBusAbstractInterface>
#include <QDBusConnection>
#include <QDBusPendingReply>
#include <QString>

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
