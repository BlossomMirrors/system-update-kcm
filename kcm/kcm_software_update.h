#pragma once

#include <KQuickConfigModule>

class SoftwareUpdateBackend;

class KCMSoftwareUpdate : public KQuickConfigModule
{
    Q_OBJECT
    Q_PROPERTY(QObject* backend READ backend CONSTANT)
public:
    explicit KCMSoftwareUpdate(QObject *parent, const KPluginMetaData &data);
    QObject* backend() const;

private:
    SoftwareUpdateBackend *m_backend;
};
