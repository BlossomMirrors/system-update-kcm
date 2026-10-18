#include "p2pbackend.h"

#include <KFormat>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

static constexpr auto P2P_SERVICE = "org.blossomos.P2pUpdated1";
static constexpr auto P2P_PATH    = "/org/blossomos/P2pUpdated1";
static constexpr auto P2P_IFACE   = "org.blossomos.P2pUpdated1";
static constexpr auto PROPS_IFACE = "org.freedesktop.DBus.Properties";
static constexpr int  SET_TIMEOUT_MS = 5 * 60 * 1000;

P2pBackend::P2pBackend(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::systemBus().connect(QString::fromLatin1(P2P_SERVICE),
                                         QString::fromLatin1(P2P_PATH),
                                         QString::fromLatin1(PROPS_IFACE),
                                         QStringLiteral("PropertiesChanged"),
                                         this,
                                         SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    refresh();
}

QString P2pBackend::storeSize() const
{
    return KFormat().formatByteSize(static_cast<double>(m_storeBytes));
}

void P2pBackend::refresh()
{
    auto msg = QDBusMessage::createMethodCall(QString::fromLatin1(P2P_SERVICE),
                                              QString::fromLatin1(P2P_PATH),
                                              QString::fromLatin1(PROPS_IFACE),
                                              QStringLiteral("GetAll"));
    msg << QString::fromLatin1(P2P_IFACE);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QVariantMap> reply = *w;
        w->deleteLater();
        if (reply.isError()) {
            m_available = false;
            Q_EMIT changed();
            return;
        }
        m_available = true;
        apply(reply.value());
    });
}

void P2pBackend::setEnabled(bool enabled)
{
    if (m_busy || m_locked)
        return;
    m_busy = true;
    Q_EMIT changed();

    auto msg = QDBusMessage::createMethodCall(QString::fromLatin1(P2P_SERVICE),
                                              QString::fromLatin1(P2P_PATH),
                                              QString::fromLatin1(P2P_IFACE),
                                              QStringLiteral("SetEnabled"));
    msg << enabled;
    msg.setInteractiveAuthorizationAllowed(true);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, SET_TIMEOUT_MS), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (reply.isError())
            qWarning() << "p2pupdated SetEnabled failed:" << reply.error().message();
        m_busy = false;
        refresh();
    });
}

void P2pBackend::onPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != QLatin1String(P2P_IFACE))
        return;
    m_available = true;
    if (!invalidated.isEmpty()) {
        refresh();
        return;
    }
    apply(changed);
}

void P2pBackend::apply(const QVariantMap &properties)
{
    if (properties.contains(QStringLiteral("Enabled")))
        m_enabled = properties.value(QStringLiteral("Enabled")).toBool();
    if (properties.contains(QStringLiteral("Locked")))
        m_locked = properties.value(QStringLiteral("Locked")).toBool();
    if (properties.contains(QStringLiteral("PeerCount")))
        m_peerCount = properties.value(QStringLiteral("PeerCount")).toInt();
    if (properties.contains(QStringLiteral("StoreBytes")))
        m_storeBytes = properties.value(QStringLiteral("StoreBytes")).toULongLong();
    Q_EMIT changed();
}
