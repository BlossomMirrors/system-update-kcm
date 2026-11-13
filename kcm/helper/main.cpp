#include "updatehelper.h"
#include "../updatehelperdbus.h"

#include <QCoreApplication>
#include <QDBusConnection>

#include <KLocalizedString>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("kcm_software_update_helper"));
    KLocalizedString::setApplicationDomain("kcm_software_update");

    UpdateHelper helper;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QLatin1String(HELPER_PATH), &helper,
                            QDBusConnection::ExportScriptableSlots
                                | QDBusConnection::ExportScriptableSignals))
        return 1;
    // Another instance already owns the running upgrade
    if (!bus.registerService(QLatin1String(HELPER_SERVICE)))
        return 1;

    return app.exec();
}
