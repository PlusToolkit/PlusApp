/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusServerLauncherMainWindow.h"

// PlusLib includes
#include <PlusCommon.h>
#include <QPlusDeviceSetSelectorWidget.h>
#include <QPlusStatusIcon.h>
#include <vtkPlusDataCollector.h>
#include <vtkPlusDeviceFactory.h>
#include <vtkPlusOpenIGTLinkServer.h>
#include <vtkPlusTransformRepository.h>

// Qt includes
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegExp>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>

// VTK includes
#include <vtkDirectory.h>

// STL includes
#include <algorithm>
#include <fstream>

// OpenIGTLinkIO includes
#include <igtlioCommandDevice.h>
#include <igtlioDevice.h>

const char* PlusServerLauncherMainWindow::PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID = "PlusRemoteDevice";

namespace
{
  void ReplaceStringInPlace(std::string& subject, const std::string& search, const std::string& replace)
  {
    size_t pos = 0;
    while ((pos = subject.find(search, pos)) != std::string::npos)
    {
      subject.replace(pos, search.length(), replace);
      pos += replace.length();
    }
  }
}

//-----------------------------------------------------------------------------
PlusServerLauncherMainWindow::PlusServerLauncherMainWindow(QWidget* parent /*=0*/, Qt::WindowFlags flags/*=0*/, bool autoConnect /*=false*/, int remoteControlServerPort/*=RemoteControlServerPortUseDefault*/)
  : QMainWindow(parent, flags)
  , m_DeviceSetSelectorWidget(NULL)
  , m_RemoteControlServerPort(remoteControlServerPort)
  , m_RemoteControlServerConnectorProcessTimer(new QTimer())
  , m_RemoteControlLogSubscribed(false)
{
  m_RemoteControlServerCallbackCommand = vtkSmartPointer<vtkCallbackCommand>::New();
  m_RemoteControlServerCallbackCommand->SetCallback(PlusServerLauncherMainWindow::OnRemoteControlServerEventReceived);
  m_RemoteControlServerCallbackCommand->SetClientData(this);

  m_RemoteControlLogMessageCallbackCommand = vtkSmartPointer<vtkCallbackCommand>::New();
  m_RemoteControlLogMessageCallbackCommand->SetCallback(PlusServerLauncherMainWindow::OnLogEvent);
  m_RemoteControlLogMessageCallbackCommand->SetClientData(this);

  // Set up UI
  ui.setupUi(this);

  // Create device set selector widget
  m_DeviceSetSelectorWidget = new QPlusDeviceSetSelectorWidget(NULL);
  m_DeviceSetSelectorWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
  m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launch server"));
  connect(m_DeviceSetSelectorWidget, SIGNAL(ConnectToDevicesByConfigFileInvoked(std::string)), this, SLOT(ConnectToDevicesByConfigFile(std::string)));

  // Create status icon
  QPlusStatusIcon* statusIcon = new QPlusStatusIcon(NULL);
  // Show only the last few thousand messages
  // (it should be enough, as all the messages are available in log files anyway)
  statusIcon->SetMaxMessageCount(3000);

  // Put the status icon in a frame with the log level selector
  ui.statusBarLayout->insertWidget(0, statusIcon);
  ui.comboBox_LogLevel->addItem("Error", QVariant(vtkPlusLogger::LOG_LEVEL_ERROR));
  ui.comboBox_LogLevel->addItem("Warning", QVariant(vtkPlusLogger::LOG_LEVEL_WARNING));
  ui.comboBox_LogLevel->addItem("Info", QVariant(vtkPlusLogger::LOG_LEVEL_INFO));
  ui.comboBox_LogLevel->addItem("Debug", QVariant(vtkPlusLogger::LOG_LEVEL_DEBUG));
  ui.comboBox_LogLevel->addItem("Trace", QVariant(vtkPlusLogger::LOG_LEVEL_TRACE));
  if (autoConnect)
  {
    ui.comboBox_LogLevel->setCurrentIndex(ui.comboBox_LogLevel->findData(QVariant(vtkPlusLogger::Instance()->GetLogLevel())));
  }
  else
  {
    ui.comboBox_LogLevel->setCurrentIndex(ui.comboBox_LogLevel->findData(QVariant(vtkPlusLogger::LOG_LEVEL_INFO)));
    vtkPlusLogger::Instance()->SetLogLevel(vtkPlusLogger::LOG_LEVEL_INFO);
  }
  connect(ui.comboBox_LogLevel, SIGNAL(currentIndexChanged(int)), this, SLOT(LogLevelChanged()));

  // Insert widgets into placeholders
  ui.centralLayout->removeWidget(ui.placeholder);
  ui.centralLayout->insertWidget(0, m_DeviceSetSelectorWidget);

  // Log basic info (Plus version, supported devices)
  std::string strPlusLibVersion = std::string(" Software version: ") + PlusCommon::GetPlusLibVersionString();
  LOG_INFO(strPlusLibVersion);
  LOG_INFO("Logging at level " << vtkPlusLogger::Instance()->GetLogLevel() << " to file: " << vtkPlusLogger::Instance()->GetLogFileName());
  vtkSmartPointer<vtkPlusDeviceFactory> deviceFactory = vtkSmartPointer<vtkPlusDeviceFactory>::New();
  std::ostringstream supportedDevices;
  deviceFactory->PrintAvailableDevices(supportedDevices, vtkIndent());
  LOG_INFO(supportedDevices.str());

  if (autoConnect)
  {
    std::string configFileName = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationFileName();
    if (configFileName.empty())
    {
      LOG_ERROR("Auto-connect failed: device set configuration file is not specified");
    }
    else
    {
      ConnectToDevicesByConfigFile(configFileName);
      if (m_DeviceSetSelectorWidget->GetConnectionSuccessful())
      {
        showMinimized();
      }
    }
  }

  // Log server host name, domain, and IP addresses
  LOG_INFO("Server host name: " << QHostInfo::localHostName().toStdString());
  if (!QHostInfo::localDomainName().isEmpty())
  {
    LOG_INFO("Server host domain: " << QHostInfo::localDomainName().toStdString());
  }

  QString ipAddresses;
  QList<QHostAddress> list = QNetworkInterface::allAddresses();
  for (int hostIndex = 0; hostIndex < list.count(); hostIndex++)
  {
    if (list[hostIndex].protocol() == QAbstractSocket::IPv4Protocol)
    {
      if (!ipAddresses.isEmpty())
      {
        ipAddresses.append(", ");
      }
      ipAddresses.append(list[hostIndex].toString());
    }
  }

  LOG_INFO("Server IP addresses: " << ipAddresses.toStdString());

  if (m_RemoteControlServerPort != PlusServerLauncherMainWindow::RemoteControlServerPortDisable)
  {
    if (m_RemoteControlServerPort == PlusServerLauncherMainWindow::RemoteControlServerPortUseDefault)
    {
      m_RemoteControlServerPort = DEFAULT_REMOTE_CONTROL_SERVER_PORT;
    }

    LOG_INFO("Start remote control server at port: " << m_RemoteControlServerPort);
    m_RemoteControlServerLogic = igtlioLogicPointer::New();
    m_RemoteControlServerLogic->AddObserver(igtlioLogic::CommandReceivedEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerLogic->AddObserver(igtlioLogic::CommandResponseReceivedEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerConnector = m_RemoteControlServerLogic->CreateConnector();
    m_RemoteControlServerConnector->AddObserver(igtlioConnector::ConnectedEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerConnector->SetTypeServer(m_RemoteControlServerPort);
    m_RemoteControlServerConnector->Start();

    ui.label_networkDetails->setText(ipAddresses + ", port " + QString::number(m_RemoteControlServerPort));

    vtkPlusLogger::Instance()->AddObserver(vtkPlusLogger::MessageLogged, m_RemoteControlLogMessageCallbackCommand);
    vtkPlusLogger::Instance()->AddObserver(vtkPlusLogger::WideMessageLogged, m_RemoteControlLogMessageCallbackCommand);
  }

  connect(ui.checkBox_writePermission, &QCheckBox::clicked, this, &PlusServerLauncherMainWindow::OnWritePermissionClicked);

  connect(m_RemoteControlServerConnectorProcessTimer, &QTimer::timeout, this, &PlusServerLauncherMainWindow::OnTimerTimeout);

  m_RemoteControlServerConnectorProcessTimer->start(5);
}

//-----------------------------------------------------------------------------
PlusServerLauncherMainWindow::~PlusServerLauncherMainWindow()
{
  LocalStopServer(); // deletes m_CurrentServerInstance

  if (m_RemoteControlServerLogic)
  {
    m_RemoteControlServerLogic->RemoveObserver(m_RemoteControlServerCallbackCommand);
  }

  if (m_RemoteControlServerConnector)
  {
    m_RemoteControlServerConnector->RemoveObserver(m_RemoteControlServerCallbackCommand);
    if (vtkPlusLogger::Instance()->HasObserver(vtkPlusLogger::MessageLogged, m_RemoteControlLogMessageCallbackCommand) ||
        vtkPlusLogger::Instance()->HasObserver(vtkPlusLogger::WideMessageLogged, m_RemoteControlLogMessageCallbackCommand))
    {
      vtkPlusLogger::Instance()->RemoveObserver(m_RemoteControlLogMessageCallbackCommand);
    }
  }

  if (m_DeviceSetSelectorWidget != NULL)
  {
    delete m_DeviceSetSelectorWidget;
    m_DeviceSetSelectorWidget = NULL;
  }

  delete m_RemoteControlServerConnectorProcessTimer;
  m_RemoteControlServerConnectorProcessTimer = nullptr;

  disconnect(ui.checkBox_writePermission, &QCheckBox::clicked, this, &PlusServerLauncherMainWindow::OnWritePermissionClicked);
  connect(m_RemoteControlServerConnectorProcessTimer, &QTimer::timeout, this, &PlusServerLauncherMainWindow::OnTimerTimeout);
}

//-----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::StartServer(const QString& configFilePath, int logLevel)
{
  QProcess* newServerProcess = new QProcess();
  m_ServerInstances[vtksys::SystemTools::GetFilenameName(configFilePath.toStdString())] = newServerProcess;
  std::string plusServerExecutable = vtkPlusConfig::GetInstance()->GetPlusExecutablePath("PlusServer");
  std::string plusServerLocation = vtksys::SystemTools::GetFilenamePath(plusServerExecutable);
  newServerProcess->setWorkingDirectory(QString(plusServerLocation.c_str()));

  connect(newServerProcess, SIGNAL(error(QProcess::ProcessError)), this, SLOT(ErrorReceived(QProcess::ProcessError)));
  connect(newServerProcess, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(ServerExecutableFinished(int, QProcess::ExitStatus)));

  // PlusServerLauncher wants at least LOG_LEVEL_INFO to parse status information from the PlusServer executable
  // Un-requested log entries that are captured from the PlusServer executable are parsed and dropped from output
  int logLevelToPlusServer = vtkPlusLogger::LOG_LEVEL_INFO;
  if (logLevel == vtkPlusLogger::LOG_LEVEL_UNDEFINED)
  {
    logLevelToPlusServer = std::max<int>(ui.comboBox_LogLevel->currentData().toInt(), logLevelToPlusServer);
  }
  else
  {
    logLevelToPlusServer = std::max<int>(logLevel, logLevelToPlusServer);
  }
  QString cmdLine = QString("\"%1\" --config-file=\"%2\" --verbose=%3").arg(plusServerExecutable.c_str()).arg(configFilePath).arg(logLevelToPlusServer);
  LOG_INFO("Server process command line: " << cmdLine.toLatin1().constData());
  newServerProcess->start(cmdLine);
  newServerProcess->waitForFinished(500);

  // During waitForFinished an error signal may be emitted, which may delete m_CurrentServerInstance,
  // therefore we need to check if m_CurrentServerInstance is still not NULL
  if (newServerProcess && newServerProcess->state() == QProcess::Running)
  {
    LOG_INFO("Server process started successfully");
    ui.comboBox_LogLevel->setEnabled(false);
    return true;
  }
  else
  {
    LOG_ERROR("Failed to start server process");
    return false;
  }
}

//----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::LocalStartServer()
{
  if (!StartServer(QString::fromStdString(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile))))
  {
    return false;
  }
  QProcess* newServerProcess = m_ServerInstances[vtksys::SystemTools::GetFilenameName(m_LocalConfigFile)];
  connect(newServerProcess, SIGNAL(readyReadStandardOutput()), this, SLOT(StdOutMsgReceived()));
  connect(newServerProcess, SIGNAL(readyReadStandardError()), this, SLOT(StdErrMsgReceived()));

  if (m_RemoteControlServerConnector)
  {
    this->SendServerStartedCommand(m_LocalConfigFile.c_str());
  }

  return true;
}

//-----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::StopServer(const QString& configFilePath)
{
  if (m_ServerInstances.find(vtksys::SystemTools::GetFilenameName(configFilePath.toStdString())) == m_ServerInstances.end())
  {
    // Server at config file isn't running
    return true;
  }

  QProcess* process = m_ServerInstances[vtksys::SystemTools::GetFilenameName(configFilePath.toStdString())];
  disconnect(process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(ErrorReceived(QProcess::ProcessError)));
  disconnect(process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(ServerExecutableFinished(int, QProcess::ExitStatus)));

  bool forcedShutdown = false;
  if (process->state() == QProcess::Running)
  {
    process->terminate();
    if (process->state() == QProcess::Running)
    {
      LOG_INFO("Server process stop request sent successfully");
    }
    const int totalTimeoutSec = 15;
    const double retryDelayTimeoutSec = 0.3;
    double timePassedSec = 0;
    // TODO : make this asynchronous so server can continue performing other actions
    while (!process->waitForFinished(retryDelayTimeoutSec * 1000))
    {
      process->terminate(); // in release mode on Windows the first terminate request may go unnoticed
      timePassedSec += retryDelayTimeoutSec;
      if (timePassedSec > totalTimeoutSec)
      {
        // graceful termination was not successful, force the process to quit
        LOG_WARNING("Server process did not stop on request for " << timePassedSec << " seconds, force it to quit now");
        process->kill();
        forcedShutdown = true;
        break;
      }
    }
    LOG_INFO("Server process stopped successfully");
    ui.comboBox_LogLevel->setEnabled(true);
  }

  delete process;
  m_ServerInstances.erase(m_ServerInstances.find(vtksys::SystemTools::GetFilenameName(configFilePath.toStdString())));

  m_Suffix.clear();
  return !forcedShutdown;
}

//----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::LocalStopServer()
{
  if (m_ServerInstances.find(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile)) == m_ServerInstances.end())
  {
    // Server at config file isn't running
    return true;
  }

  QProcess* process = m_ServerInstances[vtksys::SystemTools::GetFilenameName(m_LocalConfigFile)];

  disconnect(process, SIGNAL(readyReadStandardOutput()), this, SLOT(StdOutMsgReceived()));
  disconnect(process, SIGNAL(readyReadStandardError()), this, SLOT(StdErrMsgReceived()));

  bool result = StopServer(QString::fromStdString(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile)));
  m_LocalConfigFile = "";

  if (m_RemoteControlServerConnector)
  {
    std::string configFileString = vtksys::SystemTools::GetFilenameName(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile));
    this->SendServerStoppedCommand(configFileString.c_str());
  }

  return result;
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ParseContent(const std::string& message)
{
  // Input is the format: message
  // Plus OpenIGTLink server listening on IPs: 169.254.100.247, 169.254.181.13, 129.100.44.163, 192.168.199.1, 192.168.233.1, 127.0.0.1 -- port 18944
  if (message.find("Plus OpenIGTLink server listening on IPs:") != std::string::npos)
  {
    m_Suffix.append(message);
    m_Suffix.append("\n");
    m_DeviceSetSelectorWidget->SetDescriptionSuffix(QString(m_Suffix.c_str()));
  }
  else if (message.find("Server status: Server(s) are running.") != std::string::npos)
  {
    m_DeviceSetSelectorWidget->SetConnectionSuccessful(true);
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Stop server"));
  }
  else if (message.find("Server status: ") != std::string::npos)
  {
    // pull off server status and display it
    this->m_DeviceSetSelectorWidget->SetDescriptionSuffix(QString(message.c_str()));
  }
}

//----------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::SendCommand(std::string device_id, std::string command, std::string content, igtl::MessageBase::MetaDataMap metaData)
{
  igtlioDeviceKeyType key(igtlioCommandConverter::GetIGTLTypeName(), device_id);
  igtlioCommandDevicePointer device = igtlioCommandDevice::SafeDownCast(m_RemoteControlServerConnector->AddDeviceIfNotPresent(key));

  igtlioCommandConverter::ContentData contentdata;
  contentdata.name = command;
  contentdata.content = content;
  device->SetContent(contentdata);
  for (igtl::MessageBase::MetaDataMap::iterator entry = metaData.begin(); entry != metaData.end(); ++entry)
  {
    device->SetMetaDataElement((*entry).first, (*entry).second.first, (*entry).second.second);
  }

  if (m_RemoteControlServerConnector->SendMessage(igtlioDeviceKeyType::CreateDeviceKey(device)) == 1)
  {
    return PLUS_SUCCESS;
  }

  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::SendCommandResponse(std::string device_id, std::string command, std::string content, igtl::MessageBase::MetaDataMap metaData)
{
  igtlioDeviceKeyType key(igtlioCommandConverter::GetIGTLTypeName(), device_id);
  igtlioCommandDevicePointer device = igtlioCommandDevice::SafeDownCast(m_RemoteControlServerConnector->GetDevice(key));

  igtlioCommandConverter::ContentData contentdata = device->GetContent();

  if (command != contentdata.name)
  {
    LOG_ERROR("Requested command response " << command << " does not match the existing query: " << contentdata.name);
    return PLUS_FAIL;
  }

  contentdata.name = command;
  contentdata.content = content;
  device->SetContent(contentdata);
  for (igtl::MessageBase::MetaDataMap::iterator entry = metaData.begin(); entry != metaData.end(); ++entry)
  {
    device->SetMetaDataElement((*entry).first, (*entry).second.first, (*entry).second.second);
  }

  if (m_RemoteControlServerConnector->SendMessage(igtlioDeviceKeyType::CreateDeviceKey(device), igtlioDevice::MESSAGE_PREFIX_RTS) == 1)
  {
    return PLUS_SUCCESS;
  }
  LOG_ERROR("Unable to send command response to client: " << device_id << ", " << command);

  return PLUS_FAIL;
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ConnectToDevicesByConfigFile(std::string aConfigFile)
{
  // Either a connect or disconnect, we always start from a clean slate: delete any previously active servers
  if (!m_LocalConfigFile.empty())
  {
    LocalStopServer();
  }

  // Disconnect
  // Empty parameter string means disconnect from device
  if (aConfigFile.empty())
  {
    LOG_INFO("Disconnect request successful");
    m_DeviceSetSelectorWidget->ClearDescriptionSuffix();
    m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launch server"));
    return;
  }

  LOG_INFO("Connect using configuration file: " << aConfigFile);

  // Connect
  m_LocalConfigFile = aConfigFile;
  if (LocalStartServer())
  {
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launching..."));
  }
  else
  {
    m_DeviceSetSelectorWidget->ClearDescriptionSuffix();
    m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
    m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Launch server"));
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::keyPressEvent(QKeyEvent* e)
{
  // If ESC key is pressed don't quit the application, just minimize
  if (e->key() == Qt::Key_Escape)
  {
    showMinimized();
  }
  else
  {
    QMainWindow::keyPressEvent(e);
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::SendServerOutputToLogger(const QByteArray& strData)
{
  typedef std::vector<std::string> StringList;

  QString string(strData);
  std::string logString(string.toLatin1().constData());

  if (logString.empty())
  {
    return;
  }

  // De-windows-ifiy
  ReplaceStringInPlace(logString, "\r\n", "\n");
  StringList tokens;

  if (logString.find('|') != std::string::npos)
  {
    PlusCommon::SplitStringIntoTokens(logString, '|', tokens, false);
    // Remove empty tokens
    for (StringList::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
      if (PlusCommon::Trim(*it).empty())
      {
        tokens.erase(it);
        it = tokens.begin();
      }
    }
    unsigned int index = 0;
    if (tokens.size() == 0)
    {
      LOG_ERROR("Incorrectly formatted message received from server. Cannot parse.");
      return;
    }

    if (vtkPlusLogger::GetLogLevelType(tokens[0]) != vtkPlusLogger::LOG_LEVEL_UNDEFINED)
    {
      vtkPlusLogger::LogLevelType logLevel = vtkPlusLogger::GetLogLevelType(tokens[index++]);
      std::string timeStamp("time???");
      if (tokens.size() > 1)
      {
        timeStamp = tokens[1];
      }
      std::string message("message???");
      if (tokens.size() > 2)
      {
        message = tokens[2];
      }
      std::string location("location???");
      if (tokens.size() > 3)
      {
        location = tokens[3];
      }

      if (location.find('(') == std::string::npos || location.find(')') == std::string::npos)
      {
        // Malformed server message, print as is
        vtkPlusLogger::Instance()->LogMessage(logLevel, message.c_str());
      }
      else
      {
        std::string file = location.substr(4, location.find_last_of('(') - 4);
        int lineNumber(0);
        std::stringstream lineNumberStr(location.substr(location.find_last_of('(') + 1, location.find_last_of(')') - location.find_last_of('(') - 1));
        lineNumberStr >> lineNumber;

        // Only parse for content if the line was successfully parsed for logging
        this->ParseContent(message);

        vtkPlusLogger::Instance()->LogMessage(logLevel, message.c_str(), file.c_str(), lineNumber, "SERVER");
      }
    }
  }
  else
  {
    PlusCommon::SplitStringIntoTokens(logString, '\n', tokens, false);
    for (StringList::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
      vtkPlusLogger::Instance()->LogMessage(vtkPlusLogger::LOG_LEVEL_INFO, *it, "SERVER");
      this->ParseContent(*it);
    }
    return;
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::StdOutMsgReceived()
{
  QByteArray strData = m_ServerInstances[vtksys::SystemTools::GetFilenameName(m_LocalConfigFile)]->readAllStandardOutput();
  SendServerOutputToLogger(strData);
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::StdErrMsgReceived()
{
  QByteArray strData = m_ServerInstances[vtksys::SystemTools::GetFilenameName(m_LocalConfigFile)]->readAllStandardError();
  SendServerOutputToLogger(strData);
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ErrorReceived(QProcess::ProcessError errorCode)
{
  const char* errorString = "unknown";
  switch ((QProcess::ProcessError)errorCode)
  {
    case QProcess::FailedToStart:
      errorString = "FailedToStart";
      break;
    case QProcess::Crashed:
      errorString = "Crashed";
      break;
    case QProcess::Timedout:
      errorString = "Timedout";
      break;
    case QProcess::WriteError:
      errorString = "WriteError";
      break;
    case QProcess::ReadError:
      errorString = "ReadError";
      break;
    case QProcess::UnknownError:
    default:
      errorString = "UnknownError";
  }
  LOG_ERROR("Server process error: " << errorString);
  m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ServerExecutableFinished(int returnCode, QProcess::ExitStatus status)
{
  if (returnCode == 0)
  {
    LOG_INFO("Server process terminated.");
  }
  else
  {
    LOG_ERROR("Server stopped unexpectedly. Return code: " << returnCode);
  }

  QProcess* finishedProcess = dynamic_cast<QProcess*>(sender());
  std::string configFileName = "";
  if (finishedProcess)
  {
    for (std::map<std::string, QProcess*>::iterator server = m_ServerInstances.begin(); server != m_ServerInstances.end(); ++server)
    {
      if ((*server).second == finishedProcess)
      {
        configFileName = (*server).first;
        break;
      }
    }
  }

  if (strcmp(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile).c_str(), configFileName.c_str()) == 0)
  {
    this->ConnectToDevicesByConfigFile("");
    ui.comboBox_LogLevel->setEnabled(true);
    m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
  }

  this->SendServerStoppedCommand(configFileName);
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::LogLevelChanged()
{
  vtkPlusLogger::Instance()->SetLogLevel(ui.comboBox_LogLevel->currentData().toInt());
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
  PlusServerLauncherMainWindow* self = reinterpret_cast<PlusServerLauncherMainWindow*>(clientData);

  igtlioLogicPointer logic = dynamic_cast<igtlioLogic*>(caller);

  switch (eventId)
  {
    case igtlioConnector::ConnectedEvent:
    {
      self->LocalLog(vtkPlusLogger::LOG_LEVEL_INFO, "Client connected.");
      break;
    }
    case igtlioLogic::CommandReceivedEvent:
    {
      if (logic == nullptr)
      {
        return;
      }
      self->OnCommandReceivedEvent(logic);
      break;
    }
    case igtlioLogic::CommandResponseReceivedEvent:
    {
      if (logic == nullptr)
      {
        return;
      }

      break;
    }
  }
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnCommandReceivedEvent(igtlioLogicPointer logic)
{
  if (!logic)
  {
    LOG_ERROR("Command event could not be read!");
  }

  for (unsigned int i = 0; i < logic->GetNumberOfDevices(); ++i)
  {
    igtlioDevicePointer device = logic->GetDevice(i);
    if (STRCASECMP(device->GetDeviceType().c_str(), "COMMAND") == 0)
    {
      igtlioCommandDevicePointer commandDevice = igtlioCommandDevice::SafeDownCast(device);
      if (commandDevice && commandDevice->MessageDirectionIsIn())
      {
        if (device == nullptr)
        {
          return;
        }
        std::string name = commandDevice->GetContent().name;
        std::string content = commandDevice->GetContent().content;
        int id = commandDevice->GetContent().id;

        this->LocalLog(vtkPlusLogger::LOG_LEVEL_INFO, std::string("Command \"") + name + "\" received.");

        if (PlusCommon::IsEqualInsensitive(name, "GetConfigFiles"))
        {
          this->GetConfigFiles(commandDevice);
          return;
        }
        else if (PlusCommon::IsEqualInsensitive(name, "AddConfigFile"))
        {
          this->AddOrUpdateConfigFile(commandDevice);
          return;
        }
        else if (PlusCommon::IsEqualInsensitive(name, "StartServer"))
        {
          this->RemoteStartServer(commandDevice);
          return;
        }
        else if (PlusCommon::IsEqualInsensitive(name, "StopServer"))
        {
          this->RemoteStopServer(commandDevice);
          return;
        }
        else if (PlusCommon::IsEqualInsensitive(name, "LogSubscribe"))
        {
          m_RemoteControlLogSubscribed = true;
          return;
        }
        else if (PlusCommon::IsEqualInsensitive(name, "LogUnsubscribe"))
        {
          m_RemoteControlLogSubscribed = false;
          return;
        }
      }
    }
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::RemoteStopServer(igtlioCommandDevicePointer clientDevice)
{
  std::string filename;
  if (!clientDevice->GetMetaDataElement("ConfigFileName", filename))
  {
    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "FAIL");
    map["ErrorMessage"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "Config file not specified.");
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }

  this->StopServer(QString::fromStdString(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(vtksys::SystemTools::GetFilenameName(filename))));

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> responseElement = vtkSmartPointer<vtkXMLDataElement>::New();
  responseElement->SetName("Response");
  responseElement->SetAttribute("ConfigFileName", filename.c_str());
  commandElement->AddNestedElement(responseElement);

  std::stringstream commandSS;
  vtkXMLUtilities::FlattenElement(commandElement, commandSS);

  // Forced stop or not, the server is down
  igtl::MessageBase::MetaDataMap map;
  map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "SUCCESS");
  map["ConfigFileName"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, filename);
  if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, commandSS.str(), map) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }
  return;
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::RemoteStartServer(igtlioCommandDevicePointer clientDevice)
{
  std::string filename;
  if (!clientDevice->GetMetaDataElement("ConfigFileName", filename))
  {
    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "FAIL");
    map["ErrorMessage"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "Config file not specified.");
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }

  // TODO: Start server will not reduce the log level of the server below LOG_INFO for remotely started servers
  // Should probably solve by separating servers according to their origin and handle each separately
  int logLevel = -1;
  std::string logLevelString = "";
  clientDevice->GetMetaDataElement("LogLevel", logLevelString);
  if (PlusCommon::StringToInt<int>(logLevelString.c_str(), logLevel) == PLUS_FAIL)
  {
    logLevel = vtkPlusLogger::LOG_LEVEL_INFO;
  }

  if (!this->StartServer(QString::fromStdString(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(vtksys::SystemTools::GetFilenameName(filename))), logLevel))
  {
    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "FAIL");
    map["ErrorMessage"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "Failed to start server process.");
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }
  else
  {
    std::string servers = this->GetServersFromConfigFile(filename);
    std::stringstream responseSS;
    vtkSmartPointer<vtkXMLDataElement> responseXML = vtkSmartPointer<vtkXMLDataElement>::New();
    responseXML->SetName("Command");
    vtkSmartPointer<vtkXMLDataElement> resultXML = vtkSmartPointer<vtkXMLDataElement>::New();
    resultXML->SetName("Result");
    resultXML->SetAttribute("ConfigFileName", filename.c_str());
    resultXML->SetAttribute("Servers", servers.c_str());
    responseXML->AddNestedElement(resultXML);
    vtkXMLUtilities::FlattenElement(responseXML, responseSS);

    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "SUCCESS");
    map["ConfigFileName"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, filename);
    map["Servers"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, servers);
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, responseSS.str(), map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::GetConfigFiles(igtlioCommandDevicePointer clientDevice)
{
  vtkSmartPointer<vtkDirectory> dir = vtkSmartPointer<vtkDirectory>::New();
  if (dir->Open(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory().c_str()) == 0)
  {
    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "SUCCESS");
    map["ErrorMessage"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "Unable to open device set directory.");
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }

  std::stringstream ss;
  for (vtkIdType i = 0; i < dir->GetNumberOfFiles(); ++i)
  {
    std::string file = dir->GetFile(i);
    std::string ext = vtksys::SystemTools::GetFilenameLastExtension(file);
    if (PlusCommon::IsEqualInsensitive(ext, ".xml"))
    {
      ss << file << ";";
    }
  }

  igtl::MessageBase::MetaDataMap map;
  map["ConfigFiles"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, ss.str());
  map["Separator"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, ";");
  map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "SUCCESS");
  if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::LocalLog(vtkPlusLogger::LogLevelType level, const std::string& message)
{
  this->statusBar()->showMessage(QString::fromStdString(message));
  LOG_DYNAMIC(message, level);
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::AddOrUpdateConfigFile(igtlioCommandDevicePointer clientDevice)
{
  std::string configFile;
  bool hasFilename = clientDevice->GetMetaDataElement("ConfigFileName", configFile);
  std::string configFileContent;
  bool hasFileContent = clientDevice->GetMetaDataElement("ConfigFileContent", configFileContent);

  // Check write permissions
  if (!ui.checkBox_writePermission->isChecked())
  {
    LOG_INFO("Request from client to add config file, but write permissions not enabled. File: " << (hasFilename ? configFile : "unknown"));

    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "FAIL");
    map["ErrorMessage"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "Write permission denied.");
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }

    return;
  }

  if (!hasFilename || !hasFileContent)
  {
    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "FAIL");
    map["ErrorMessage"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "Required metadata \'ConfigFileName\' and/or \'ConfigFileContent\' missing.");
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }

  // Strip any path sent over
  configFile = vtksys::SystemTools::GetFilenameName(configFile);
  // If filename already exists, check overwrite permissions
  if (vtksys::SystemTools::FileExists(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile)))
  {
    if (!ui.checkBox_overwritePermission->isChecked())
    {
      std::string oldConfigFile = configFile;
      int i = 0;
      while (vtksys::SystemTools::FileExists(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile)))
      {
        configFile = oldConfigFile + "[" + PlusCommon::ToString<int>(i) + "]";
        i++;
      }
      LOG_INFO("Config file: " << oldConfigFile << " already exists. Changing to: " << configFile);
    }
    else
    {
      // Overwrite, backup in case writing of new file fails
      vtksys::SystemTools::CopyAFile(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile),
                                     vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile) + ".bak");
      vtksys::SystemTools::RemoveFile(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile));
    }
  }

  std::fstream file(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile), std::fstream::out);
  if (!file.is_open())
  {
    if (vtksys::SystemTools::FileExists(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile) + ".bak"))
    {
      // Restore backup
      vtksys::SystemTools::CopyAFile(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile) + ".bak",
                                     vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile));
    }

    igtl::MessageBase::MetaDataMap map;
    map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "FAIL");
    map["ErrorMessage"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "Unable to write to device set configuration directory.");
    if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
      if (vtksys::SystemTools::FileExists(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile) + ".bak"))
      {
        vtksys::SystemTools::RemoveFile(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile) + ".bak");
      }
    }
    return;
  }

  file << configFileContent;
  file.close();

  igtl::MessageBase::MetaDataMap map;
  map["Status"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, "SUCCESS");
  map["ConfigFileName"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, configFile);
  if (this->SendCommandResponse(clientDevice->GetDeviceName(), clientDevice->GetContent().name, "", map) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }

  if (vtksys::SystemTools::FileExists(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile) + ".bak"))
  {
    vtksys::SystemTools::RemoveFile(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(configFile) + ".bak");
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnWritePermissionClicked()
{
  ui.checkBox_overwritePermission->setEnabled(ui.checkBox_writePermission->isChecked());
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnTimerTimeout()
{
  if (m_RemoteControlServerConnector != nullptr)
  {
    m_RemoteControlServerConnector->PeriodicProcess();
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::SendServerStartedCommand(std::string configFilePath)
{
  LOG_TRACE("Sending server started command");

  std::string filename = vtksys::SystemTools::GetFilenameName(configFilePath);
  std::string logLevelString = PlusCommon::ToString(this->ui.comboBox_LogLevel->currentData().Int);

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> startServerElement = vtkSmartPointer<vtkXMLDataElement>::New();
  startServerElement->SetName("ServerStarted");
  startServerElement->SetAttribute("LogLevel", logLevelString.c_str());
  startServerElement->SetAttribute("ConfigFileName", filename.c_str());

  vtkSmartPointer<vtkXMLDataElement> configFileElement = NULL;
  configFileElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(configFilePath.c_str()));
  if (configFileElement)
  {
    std::string ports = this->GetServersFromConfigFile(filename);
    startServerElement->SetAttribute("Servers", ports.c_str());
    startServerElement->AddNestedElement(configFileElement);
  }
  commandElement->AddNestedElement(startServerElement);

  igtl::MessageBase::MetaDataMap map;
  map["LogLevel"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, startServerElement->GetAttribute("LogLevel"));
  map["ConfigFileName"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, startServerElement->GetAttribute("ConfigFileName"));
  if (startServerElement->GetAttribute("Servers"))
  {
    map["Servers"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, startServerElement->GetAttribute("Servers"));
  }

  std::stringstream commandStream;
  vtkXMLUtilities::FlattenElement(commandElement, commandStream);
  this->SendCommand(PlusServerLauncherMainWindow::PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID, "ServerStarted", commandStream.str(), map);

}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::SendServerStoppedCommand(std::string configFilePath)
{
  LOG_TRACE("Sending server stopped command");

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> serverStoppedElement = vtkSmartPointer<vtkXMLDataElement>::New();
  serverStoppedElement->SetName("ServerStopped");
  serverStoppedElement->SetAttribute("ConfigFileName", configFilePath.c_str());
  commandElement->AddNestedElement(serverStoppedElement);

  igtl::MessageBase::MetaDataMap map;
  map["ConfigFileName"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, configFilePath);

  std::stringstream commandStream;
  vtkXMLUtilities::FlattenElement(commandElement, commandStream);
  this->SendCommand(PlusServerLauncherMainWindow::PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID, "ServerStopped", commandStream.str(), map);
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnLogEvent(vtkObject* caller, unsigned long event, void* clientData, void* callData)
{
  PlusServerLauncherMainWindow* self = reinterpret_cast<PlusServerLauncherMainWindow*>(clientData);

  // Return if we are not connected. No client to send log messages to
  if (!self->m_RemoteControlServerConnector || !self->m_RemoteControlServerConnector->IsConnected())
  {
    return;
  }

  // Return if the client has not subscribed to log messages
  if (!self->m_RemoteControlLogSubscribed)
  {
    return;
  }

  QString logMessage = QString();
  if (event == vtkPlusLogger::MessageLogged)
  {
    const char* logMessageChar = static_cast<char*>(callData);
    logMessage = logMessage.fromLatin1(logMessageChar);
  }
  else if (event == vtkPlusLogger::WideMessageLogged)
  {
    const wchar_t* logMessageWChar = static_cast<wchar_t*>(callData);
    logMessage = logMessage.fromWCharArray(logMessageWChar);
  }

  if (logMessage.isEmpty())
  {
    return;
  }

  QStringList tokens = logMessage.split('|', QString::SkipEmptyParts);
  if (tokens.size() == 0)
  {
    return;
  }

  std::string logLevel = tokens[0].toStdString();
  std::string messageOrigin;
  if (tokens.size() > 2 && logMessage.toStdString().find("SERVER>") != std::string::npos)
  {
    messageOrigin = "SERVER";
  }
  else
  {
    messageOrigin = "LAUNCHER";
  }

  std::stringstream message;
  for (int i = 1; i < tokens.size(); ++i)
  {
    message << "|" << tokens[i].toStdString();
  }
  std::string logMessageString = message.str();

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> messageElement = vtkSmartPointer<vtkXMLDataElement>::New();
  messageElement->SetName("LogMessage");
  messageElement->SetAttribute("Message", logMessageString.c_str());
  messageElement->SetAttribute("LogLevel", logLevel.c_str());
  messageElement->SetAttribute("Origin", messageOrigin.c_str());
  commandElement->AddNestedElement(messageElement);

  std::stringstream messageCommand;
  vtkXMLUtilities::FlattenElement(commandElement, messageCommand);

  igtl::MessageBase::MetaDataMap map;
  map["Message"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, logMessageString);
  map["LogLevel"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, logLevel);
  map["Origin"] = std::pair<IANA_ENCODING_TYPE, std::string>(IANA_TYPE_US_ASCII, messageOrigin);
  self->SendCommand(PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID, "LogMessage", messageCommand.str(), map);
}

//---------------------------------------------------------------------------
std::string PlusServerLauncherMainWindow::GetServersFromConfigFile(std::string filename)
{

  std::string ports = "";

  std::string filenameAndPath = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(filename);

  vtkSmartPointer<vtkXMLDataElement> configFileElement = NULL;
  configFileElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(filenameAndPath.c_str()));

  if (configFileElement)
  {
    for (int i = 0; i < configFileElement->GetNumberOfNestedElements(); ++i)
    {
      vtkXMLDataElement* nestedElement = configFileElement->GetNestedElement(i);
      if (strcmp(nestedElement->GetName(), "PlusOpenIGTLinkServer") == 0)
      {
        std::stringstream serverNamePrefix;
        const char* outputChannelId = nestedElement->GetAttribute("OutputChannelId");
        if (outputChannelId)
        {
          serverNamePrefix << outputChannelId;
        }
        else
        {
          serverNamePrefix << "PlusOpenIGTLinkServer";
        }
        serverNamePrefix << ":";
        const char* port = nestedElement->GetAttribute("ListeningPort");
        if (port)
        {
          ports += serverNamePrefix.str() + std::string(port) + ";";
        }
      }
    }

  }
  return ports;
}
