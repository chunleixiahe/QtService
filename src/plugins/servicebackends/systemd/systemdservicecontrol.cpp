#include "systemdservicecontrol.h"
#include <unistd.h>
#include <QtCore/QBuffer>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QRegularExpression>
#include <QtService/private/logging_p.h>
using namespace QtService;

SystemdServiceControl::SystemdServiceControl(QString &&serviceId, QObject *parent) :
	ServiceControl{std::move(serviceId), parent}
{}

ServiceControl::SupportFlags SystemdServiceControl::supportFlags() const
{
	return SupportsStartStop |
			SupportsReload |
			SupportsEnableDisable |
			SupportsStatus |
			SupportsCustomCommands |
			SupportsBlockingNonBlocking;
}

ServiceControl::ServiceStatus SystemdServiceControl::status() const
{
	auto svcName = serviceId().toUtf8();
	auto svcType = serviceId().mid(serviceName().size() + 1);
	if(svcType.isEmpty()) {
		svcType = QStringLiteral("service");
		svcName += '.' + svcType.toUtf8();
	}

	QByteArray data;
	if(runSystemctl("list-units", QStringList {
						QStringLiteral("--all"),
						QStringLiteral("--full"),
						QStringLiteral("--no-pager"),
						QStringLiteral("--plain"),
						QStringLiteral("--all"),
						QStringLiteral("--no-legend"),
						QStringLiteral("--type=") + svcType
					}, &data, true) != EXIT_SUCCESS)
		return ServiceStatusUnknown;

	QBuffer buffer{&data};
	buffer.open(QIODevice::ReadOnly);
	while(!buffer.atEnd()) {
		// read the line and check if it is this service, and if yes "parse" the line and verify again
		auto line = buffer.readLine();
		if(!line.startsWith(svcName))
			continue;
		auto lineData = line.simplified().split(' ');
		if(lineData.size() < 3 || lineData[0] != svcName)
			continue;

		// found correct service! now read the status
		const auto &svcState = lineData[2];
		if(svcState == "active")
			return ServiceRunning;
		else if(svcState == "reloading")
			return ServiceReloading;
		else if(svcState == "inactive")
			return ServiceStopped;
		else if(svcState == "failed")
			return ServiceErrored;
		else if(svcState == "activating")
			return ServiceStarting;
		else if(svcState == "deactivating")
			return ServiceStopping;
		else {
			qCWarning(logQtService) << "Unknown service state" << svcState << "for service" << svcName;
			return ServiceStatusUnknown;
		}
	}

	qCWarning(logQtService) << "Service" << svcName << "was not found as systemd service";
	return ServiceStatusUnknown;
}

bool SystemdServiceControl::isEnabled() const
{
	return false; //TODO implement
}

QString SystemdServiceControl::backend() const
{
	return QStringLiteral("systemd");
}

QVariant SystemdServiceControl::callGenericCommand(const QByteArray &kind, const QVariantList &args)
{
	QStringList sArgs;
	sArgs.reserve(args.size());
	for(const auto &arg : args)
		sArgs.append(arg.toString());
	return runSystemctl(kind, sArgs);
}

bool SystemdServiceControl::start()
{
	return runSystemctl("start") == EXIT_SUCCESS;
}

bool SystemdServiceControl::stop()
{
	return runSystemctl("stop") == EXIT_SUCCESS;
}

bool SystemdServiceControl::reload()
{
	return runSystemctl("reload") == EXIT_SUCCESS;
}

bool SystemdServiceControl::enable()
{
	return runSystemctl("enable") == EXIT_SUCCESS;
}

bool SystemdServiceControl::disable()
{
	return runSystemctl("disable") == EXIT_SUCCESS;
}

QString SystemdServiceControl::serviceName() const
{
	const static QRegularExpression regex(QStringLiteral(R"__((.+)\.(?:service|socket|device|mount|automount|swap|target|path|timer|slice|scope))__"));
	const auto svcId = serviceId();
	auto match = regex.match(svcId);
	if(match.hasMatch())
		return match.captured(1);
	else
		return svcId;
}

int SystemdServiceControl::runSystemctl(const QByteArray &command, const QStringList &extraArgs, QByteArray *outData, bool noPrepare) const
{
	const auto systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
	if(systemctl.isEmpty()) {
		qCWarning(logQtService) << "Failed to find systemctl executable";
		return -1;
	}

	QProcess process;
	process.setProgram(systemctl);

	QStringList args;
	args.reserve(extraArgs.size() + 4);
	if(::geteuid() == 0)
		args.append(QStringLiteral("--system"));
	else
		args.append(QStringLiteral("--user"));
	args.append(QString::fromUtf8(command));
	if(!noPrepare) {
		args.append(serviceId());
		if(!isBlocking())
			args.append(QStringLiteral("--no-block"));
	}
	args.append(extraArgs);
	process.setArguments(args);

	process.setStandardInputFile(QProcess::nullDevice());
	if(!outData)
		process.setStandardOutputFile(QProcess::nullDevice());
	process.setProcessChannelMode(QProcess::ForwardedErrorChannel);

	process.start(QProcess::ReadOnly);
	if(process.waitForFinished(isBlocking() ? -1 : 2500)) {//non-blocking calls should finish within two seconds
		if(outData)
			*outData = process.readAllStandardOutput();
		if(process.exitStatus() == QProcess::NormalExit) {
			auto code = process.exitCode();
			if(code != EXIT_SUCCESS)
				qCWarning(logQtService) << "systemctl failed with exit code:" << code;
			return code;
		} else {
			qCWarning(logQtService).noquote() << "systemctl crashed with error:" << process.errorString();
			return 128 + process.error();
		}
	} else {
		qCWarning(logQtService).noquote() << "systemctl did not exit in time";
		return -1;
	}
}