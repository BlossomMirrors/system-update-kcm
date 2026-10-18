#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <qqmlintegration.h>

class QDBusPendingCallWatcher;

class P2pBackend : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool enabled   READ enabled   NOTIFY changed)
    Q_PROPERTY(bool locked    READ locked    NOTIFY changed)
    Q_PROPERTY(bool busy      READ busy      NOTIFY changed)
    Q_PROPERTY(int  peerCount READ peerCount NOTIFY changed)
    Q_PROPERTY(QString storeSize READ storeSize NOTIFY changed)

public:
    explicit P2pBackend(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool enabled()   const { return m_enabled; }
    bool locked()    const { return m_locked; }
    bool busy()      const { return m_busy; }
    int  peerCount() const { return m_peerCount; }
    QString storeSize() const;

    Q_INVOKABLE void setEnabled(bool enabled);
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void apply(const QVariantMap &properties);

    bool    m_available = false;
    bool    m_enabled   = false;
    bool    m_locked    = false;
    bool    m_busy      = false;
    int     m_peerCount = 0;
    quint64 m_storeBytes = 0;
};
