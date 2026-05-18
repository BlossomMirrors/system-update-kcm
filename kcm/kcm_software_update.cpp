#include "kcm_software_update.h"
#include "softwareupdatebackend.h"
#include <KPluginFactory>
#include <QTimer>

K_PLUGIN_CLASS_WITH_JSON(KCMSoftwareUpdate, "kcm_software_update.json")

KCMSoftwareUpdate::KCMSoftwareUpdate(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
    , m_backend(new SoftwareUpdateBackend(this))
{
    setButtons(Help);
    // Defer checkForUpdates until the event loop is running
    QTimer::singleShot(0, m_backend, &SoftwareUpdateBackend::checkForUpdates);
}

QObject* KCMSoftwareUpdate::backend() const
{
    return m_backend;
}

#include "kcm_software_update.moc"
