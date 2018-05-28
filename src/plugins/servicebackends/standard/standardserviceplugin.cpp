#include "standardserviceplugin.h"
#include "standardservicebackend.h"
#include "standardservicecontrol.h"

StandardServicePlugin::StandardServicePlugin(QObject *parent) :
	QObject{parent}
{}

QtService::ServiceBackend *StandardServicePlugin::createServiceBackend(const QString &provider, QtService::Service *service)
{
	if(provider == QStringLiteral("standard"))
		return new StandardServiceBackend{service};
	else
		return nullptr;
}

QtService::ServiceControl *StandardServicePlugin::createServiceControl(const QString &backend, QString &&serviceId, QObject *parent)
{
	if(backend == QStringLiteral("standard"))
		return new StandardServiceControl{std::move(serviceId), parent};
	else
		return nullptr;
}