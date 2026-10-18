#pragma once

#include <KQuickConfigModule>

class SoftwareUpdateBackend;
class P2pBackend;

class KCMSoftwareUpdate : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(QObject* backend READ backend CONSTANT)
    Q_PROPERTY(QObject* p2p READ p2p CONSTANT)
public:
    explicit KCMSoftwareUpdate(QObject *parent, const KPluginMetaData &data);
    QObject* backend() const;
    QObject* p2p() const;

private:
    SoftwareUpdateBackend *m_backend;
    P2pBackend *m_p2p;
};
