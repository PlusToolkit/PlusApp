/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusServerLauncherMainWindow.h"

// PlusLib includes
#include <igsioCommon.h>
#include <QPlusDeviceSetSelectorWidget.h>
#include <QPlusStatusIcon.h>
#include <vtkPlusDataCollector.h>
#include <vtkPlusDeviceFactory.h>
#include <vtkPlusOpenIGTLinkServer.h>
#include <vtkIGSIOTransformRepository.h>

// Qt includes
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
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
#include <igtlioDevice.h>
#include <igtlioCommand.h>

enum ServerTableColumns
{
  ID,
  Name,
  Description,
  Button,
  ColumnCount,
};

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

const int SYSTEM_TRAY_MESSAGE_TIMEOUT_MS = 1000;

//-----------------------------------------------------------------------------
PlusServerLauncherMainWindow::PlusServerLauncherMainWindow(QWidget* parent /*=0*/, Qt::WindowFlags flags/*=0*/, bool autoConnect /*=false*/, int remoteControlServerPort/*=RemoteControlServerPortUseDefault*/)
  : QMainWindow(parent, flags)
  , m_DeviceSetSelectorWidget(NULL)
  , m_RemoteControlServerPort(remoteControlServerPort)
  , m_RemoteControlServerConnectorProcessTimer(new QTimer())
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

  // System tray icon
  m_SystemTrayMenu = new QMenu(this);
  m_SystemTrayShowAction = m_SystemTrayMenu->addAction("Show Plus Server Launcher", this, SLOT(show()));
  m_SystemTrayHideAction = m_SystemTrayMenu->addAction("Minimize to tray", this, SLOT(hide()));
  m_SystemTrayMenu->addSeparator();
  m_SystemTrayMenu->addAction("Open log folder", this, SLOT(OpenLogFolderClicked()));
  m_SystemTrayMenu->addAction("Open latest log file", this, SLOT(LatestLogClicked()));
  m_SystemTrayMenu->addSeparator();
  m_SystemTrayMenu->addAction("Exit", this, SLOT(close()));

  m_SystemTrayIcon = new QSystemTrayIcon(QIcon(":/icons/Resources/icon_ConnectLarge.ico"), this);
  std::string plusVersionString = PlusCommon::GetPlusLibVersionString();
  m_SystemTrayIcon->setToolTip(QString("Plus Server Launcher - %1").arg(plusVersionString.c_str()));
  m_SystemTrayIcon->setContextMenu(m_SystemTrayMenu);
  m_SystemTrayIcon->show();
  connect(m_SystemTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(SystemTrayIconActivated(QSystemTrayIcon::ActivationReason)));
  connect(m_SystemTrayIcon, SIGNAL(messageClicked()), this, SLOT(SystemTrayMessageClicked()));

  // Create status icon
  QPlusStatusIcon* statusIcon = new QPlusStatusIcon(NULL);
  // Show only the last few thousand messages
  // (it should be enough, as all the messages are available in log files anyway)
  statusIcon->SetMaxMessageCount(3000);
  statusIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

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
  std::string strPlusLibVersion = std::string(" Software version: ") + plusVersionString;
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

  // Initialize server table
  ui.serverTable->setColumnCount(ServerTableColumns::ColumnCount);
  ui.serverTable->setHorizontalHeaderItem(ServerTableColumns::ID, new QTableWidgetItem("ID"));
  ui.serverTable->setHorizontalHeaderItem(ServerTableColumns::Name, new QTableWidgetItem("Name"));
  ui.serverTable->setHorizontalHeaderItem(ServerTableColumns::Description, new QTableWidgetItem("Description"));
  ui.serverTable->setHorizontalHeaderItem(ServerTableColumns::Button, new QTableWidgetItem(" "));
  ui.serverTable->horizontalHeader()->setSectionResizeMode(ServerTableColumns::ID, QHeaderView::Stretch);
  ui.serverTable->horizontalHeader()->setSectionResizeMode(ServerTableColumns::Name, QHeaderView::Interactive);
  ui.serverTable->horizontalHeader()->setSectionResizeMode(ServerTableColumns::Description, QHeaderView::Stretch);
  ui.serverTable->horizontalHeader()->setSectionResizeMode(ServerTableColumns::Button, QHeaderView::ResizeToContents);

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
    m_RemoteControlServerLogic->AddObserver(igtlioCommand::CommandReceivedEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerLogic->AddObserver(igtlioCommand::CommandResponseEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerConnector = m_RemoteControlServerLogic->CreateConnector();
    m_RemoteControlServerConnector->AddObserver(igtlioConnector::ConnectedEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerConnector->AddObserver(igtlioConnector::ClientConnectedEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerConnector->AddObserver(igtlioConnector::ClientDisconnectedEvent, m_RemoteControlServerCallbackCommand);
    m_RemoteControlServerConnector->SetTypeServer(m_RemoteControlServerPort);
    m_RemoteControlServerConnector->Start();

    ui.label_networkDetails->setText(ipAddresses + ", port " + QString::number(m_RemoteControlServerPort));

    vtkPlusLogger::Instance()->AddObserver(vtkPlusLogger::MessageLogged, m_RemoteControlLogMessageCallbackCommand);
    vtkPlusLogger::Instance()->AddObserver(vtkPlusLogger::WideMessageLogged, m_RemoteControlLogMessageCallbackCommand);
  }

  connect(ui.checkBox_writePermission, &QCheckBox::clicked, this, &PlusServerLauncherMainWindow::OnWritePermissionClicked);

  connect(m_RemoteControlServerConnectorProcessTimer, &QTimer::timeout, this, &PlusServerLauncherMainWindow::OnTimerTimeout);

  connect(ui.pushButton_LatestLog, &QPushButton::clicked, this, &PlusServerLauncherMainWindow::LatestLogClicked);

  m_RemoteControlServerConnectorProcessTimer->start(5);

  ReadConfiguration();
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

  // Close all currently running servers
  std::deque<ServerInfo> runningServers = m_ServerInstances;
  for (std::deque<ServerInfo>::iterator serverIt = runningServers.begin(); serverIt != runningServers.end(); ++serverIt)
  {
    StopServer(QString::fromStdString(serverIt->Filename));
  }

  if (m_DeviceSetSelectorWidget != NULL)
  {
    delete m_DeviceSetSelectorWidget;
    m_DeviceSetSelectorWidget = NULL;
  }

  delete m_RemoteControlServerConnectorProcessTimer;
  m_RemoteControlServerConnectorProcessTimer = nullptr;

  disconnect(ui.checkBox_writePermission, &QCheckBox::clicked, this, &PlusServerLauncherMainWindow::OnWritePermissionClicked);
  disconnect(m_RemoteControlServerConnectorProcessTimer, &QTimer::timeout, this, &PlusServerLauncherMainWindow::OnTimerTimeout);
  disconnect(ui.pushButton_LatestLog, &QPushButton::clicked, this, &PlusServerLauncherMainWindow::LatestLogClicked);

  WriteConfiguration();
}

//-----------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::ReadConfiguration()
{
  std::string applicationConfigurationFilePath = vtkPlusConfig::GetInstance()->GetApplicationConfigurationFilePath();

  // Read configuration from file
  vtkSmartPointer<vtkXMLDataElement> applicationConfigurationRoot;
  if (vtksys::SystemTools::FileExists(applicationConfigurationFilePath.c_str(), true))
  {
    applicationConfigurationRoot.TakeReference(vtkXMLUtilities::ReadElementFromFile(applicationConfigurationFilePath.c_str()));
  }

  int currentTab = 0;
  applicationConfigurationRoot->GetScalarAttribute("CurrentTab", currentTab);
  ui.tabWidget->setCurrentIndex(currentTab);

  const char* hideOnStartupValue = applicationConfigurationRoot->GetAttribute("HideOnStartup");
  bool hideOnStartup = false;
  if (hideOnStartupValue && STRCASECMP(hideOnStartupValue, "True") == 0)
  {
    hideOnStartup = true;
  }
  ui.checkBox_startMinimized->setChecked(hideOnStartup);

  const char* showNotificationsValue = applicationConfigurationRoot->GetAttribute("ShowNotifications");
  bool showNotifications = true;
  if (showNotificationsValue && STRCASECMP(showNotificationsValue, "True") == 0)
  {
    showNotifications = true;
  }
  else
  {
    showNotifications = false;
  }
  ui.checkBox_showNotifications->setChecked(showNotifications);

  const char* minimizeOnCloseValue = applicationConfigurationRoot->GetAttribute("MinimizeOnClose");
  bool minimizeOnClose = false;
  if (minimizeOnCloseValue && STRCASECMP(minimizeOnCloseValue, "True") == 0)
  {
    minimizeOnClose = true;
  }
  ui.checkBox_minimizeOnClose->setChecked(minimizeOnClose);

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::WriteConfiguration()
{
  std::string applicationConfigurationFilePath = vtkPlusConfig::GetInstance()->GetApplicationConfigurationFilePath();

  // Read configuration from file
  vtkSmartPointer<vtkXMLDataElement> applicationConfigurationRoot;
  if (vtksys::SystemTools::FileExists(applicationConfigurationFilePath.c_str(), true))
  {
    applicationConfigurationRoot.TakeReference(vtkXMLUtilities::ReadElementFromFile(applicationConfigurationFilePath.c_str()));
  }

  applicationConfigurationRoot->SetIntAttribute("CurrentTab", ui.tabWidget->currentIndex());
  applicationConfigurationRoot->SetAttribute("HideOnStartup", ui.checkBox_startMinimized->isChecked() ? "True" : "False");
  applicationConfigurationRoot->SetAttribute("ShowNotifications", ui.checkBox_showNotifications->isChecked() ? "True" : "False");
  applicationConfigurationRoot->SetAttribute("MinimizeOnClose", ui.checkBox_minimizeOnClose->isChecked() ? "True" : "False");

  // Write configuration to file
  igsioCommon::XML::PrintXML(applicationConfigurationFilePath.c_str(), applicationConfigurationRoot);

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::StartServer(const QString& configFilePath, int logLevel)
{
  QProcess* newServerProcess = new QProcess();
  ServerInfo newServerInfo(vtksys::SystemTools::GetFilenameName(configFilePath.toStdString()), newServerProcess);
  m_ServerInstances.push_back(newServerInfo);

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

  // During waitForFinished an error signal may be emitted, which may delete newServerProcess,
  // therefore we need to check if the current server process still exists
  QString baseFileName = QString::fromStdString(vtksys::SystemTools::GetFilenameName(configFilePath.toStdString()));
  if (GetServerInfoFromID(newServerInfo.ID).Process &&
    newServerProcess && newServerProcess->state() == QProcess::Running)
  {
    LOG_INFO("Server process started successfully");
    connect(newServerInfo.Process, SIGNAL(readyReadStandardOutput()), this, SLOT(StdOutMsgReceived()));
    connect(newServerInfo.Process, SIGNAL(readyReadStandardError()), this, SLOT(StdErrMsgReceived()));
    ui.comboBox_LogLevel->setEnabled(false);
    UpdateRemoteServerTable();

    ShowNotification(QString("Configuration file: %1").arg(baseFileName), "Server starting");

    return true;
  }
  else
  {
    LOG_ERROR("Failed to start server process");

    ShowNotification(QString("Configuration file: %1").arg(baseFileName), "Failed to start server");

    return false;
  }
}

//----------------------------------------------------------------------------
PlusServerLauncherMainWindow::ServerInfo PlusServerLauncherMainWindow::GetServerInfoFromID(std::string id)
{
  for (std::deque<ServerInfo>::iterator serverIt = m_ServerInstances.begin(); serverIt != m_ServerInstances.end(); ++serverIt)
  {
    if (id == serverIt->ID)
    {
      return *serverIt;
    }
  }
  return ServerInfo();
}

//----------------------------------------------------------------------------
PlusServerLauncherMainWindow::ServerInfo PlusServerLauncherMainWindow::GetServerInfoFromFilename(std::string filename)
{
  for (std::deque<ServerInfo>::iterator serverIt = m_ServerInstances.begin(); serverIt != m_ServerInstances.end(); ++serverIt)
  {
    if (filename == serverIt->Filename)
    {
      return *serverIt;
    }
  }
  return ServerInfo();
}

//----------------------------------------------------------------------------
PlusServerLauncherMainWindow::ServerInfo PlusServerLauncherMainWindow::GetServerInfoFromProcess(QProcess* process)
{
  for (std::deque<ServerInfo>::iterator serverIt = m_ServerInstances.begin(); serverIt != m_ServerInstances.end(); ++serverIt)
  {
    if (serverIt->Process == process)
    {
      return *serverIt;
    }
  }
  return ServerInfo();
}

//----------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::RemoveServerProcess(QProcess* process)
{
  for (std::deque<ServerInfo>::iterator serverIt = m_ServerInstances.begin(); serverIt != m_ServerInstances.end(); ++serverIt)
  {
    if (serverIt->Process == process)
    {
      m_ServerInstances.erase(serverIt);
      UpdateRemoteServerTable();
      return PLUS_SUCCESS;
    }
  }
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::LocalStartServer()
{
  std::string filename = vtksys::SystemTools::GetFilenameName(m_LocalConfigFile);
  if (!StartServer(QString::fromStdString(filename)))
  {
    return false;
  }

  ServerInfo newServerInfo = GetServerInfoFromFilename(filename);
  if (m_RemoteControlServerConnector)
  {
    SendServerStartedCommand(newServerInfo);
  }

  return true;
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::UpdateRemoteServerTable()
{
  ui.serverTable->clearContents();
  ui.serverTable->setRowCount(0);

  for (std::deque<ServerInfo>::iterator server = m_ServerInstances.begin(); server != m_ServerInstances.end(); ++server)
  {
    std::string filename = server->Filename;
    std::string filePath = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(vtksys::SystemTools::GetFilenameName(filename));

    QFile file(QString::fromStdString(filePath));
    QFileInfo fileInfo(QString::fromStdString(filePath));
    QDomDocument doc;

    QString name = QString::fromStdString(filename);
    QString description;

    if (doc.setContent(&file))
    {
      QDomElement docElem = doc.documentElement();

      // Check if the root element is PlusConfiguration and contains a DeviceSet child
      if (!docElem.tagName().compare("PlusConfiguration", Qt::CaseInsensitive))
      {
        // Add the name attribute to the first node named DeviceSet to the combo box
        QDomNodeList list(doc.elementsByTagName("DeviceSet"));
        if (list.count() > 0)
        {
          // If it has a DeviceSet children then use the first one
          QDomElement elem = list.at(0).toElement();
          name = elem.attribute("Name");
          description = elem.attribute("Description");
        }
      }
    }

    int row = ui.serverTable->rowCount();
    ui.serverTable->insertRow(row);

    QTableWidgetItem* idItem = new QTableWidgetItem();
    idItem->setText(server->ID.c_str());
    idItem->setData(Qt::UserRole, QString::fromStdString(server->ID.c_str()));
    idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
    ui.serverTable->setItem(row, ServerTableColumns::ID, idItem);

    QTableWidgetItem* nameItem = new QTableWidgetItem();
    nameItem->setText(name);
    nameItem->setData(Qt::UserRole, QString::fromStdString(server->ID.c_str()));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    ui.serverTable->setItem(row, ServerTableColumns::Name, nameItem);

    QTableWidgetItem* descriptionItem = new QTableWidgetItem();
    descriptionItem->setText(description);
    descriptionItem->setFlags(descriptionItem->flags() & ~Qt::ItemIsEditable);
    ui.serverTable->setItem(row, ServerTableColumns::Description, descriptionItem);

    QPushButton* stopServerButton = new QPushButton("Stop");
    ui.serverTable->setCellWidget(row, ServerTableColumns::Button, stopServerButton);
    connect(stopServerButton, SIGNAL(clicked()), this, SLOT(StopRemoteServerButtonClicked()));
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::StopRemoteServerButtonClicked()
{
  QPushButton* pressedButton = (QPushButton*)sender();
  pressedButton->setEnabled(false);

  int row = ui.serverTable->indexAt(pressedButton->pos()).row();
  ServerInfo info = GetServerInfoFromID(ui.serverTable->item(row, 0)->data(Qt::UserRole).toString().toStdString());
  QString fileName = info.Filename.c_str();
  StopServer(fileName);
}

//-----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::StopServer(const QString& configFilePath)
{
  ServerInfo info = GetServerInfoFromFilename(vtksys::SystemTools::GetFilenameName(configFilePath.toStdString()));
  QProcess* process = info.Process;
  if (!process)
  {
    // Server at config file isn't running
    return true;
  }

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

  disconnect(process, SIGNAL(readyReadStandardOutput()), this, SLOT(StdOutMsgReceived()));
  disconnect(process, SIGNAL(readyReadStandardError()), this, SLOT(StdErrMsgReceived()));
  disconnect(process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(ErrorReceived(QProcess::ProcessError)));
  disconnect(process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(ServerExecutableFinished(int, QProcess::ExitStatus)));

  RemoveServerProcess(process);
  delete process;

  if (m_RemoteControlServerConnector)
  {
    std::string configFileString = vtksys::SystemTools::GetFilenameName(vtksys::SystemTools::GetFilenameName(configFilePath.toStdString()));
    SendServerStoppedCommand(info);
  }

  m_Suffix.clear();
  return !forcedShutdown;
}

//----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::LocalStopServer()
{
  ServerInfo info = GetServerInfoFromFilename(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile));
  QProcess* process = info.Process;
  if (!process)
  {
    // Server at config file isn't running
    return true;
  }

  bool result = StopServer(QString::fromStdString(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile)));
  m_LocalConfigFile = "";

  return result;
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ParseContent(const std::string& message)
{
  QProcess* process = qobject_cast<QProcess*>(QObject::sender());
  if (!process)
  {
    return;
  }

  bool localConfigFile = true;
  ServerInfo info = GetServerInfoFromProcess(process);
  if (info.Filename != vtksys::SystemTools::GetFilenameName(m_LocalConfigFile))
  {
    localConfigFile = false;
  }

  // Input is the format: message
  // Plus OpenIGTLink server listening on IPs: 169.254.100.247, 169.254.181.13, 129.100.44.163, 192.168.199.1, 192.168.233.1, 127.0.0.1 -- port 18944
  if (message.find("Plus OpenIGTLink server listening on IPs:") != std::string::npos)
  {
    if (localConfigFile)
    {
      m_Suffix.append(message);
      m_Suffix.append("\n");
      m_DeviceSetSelectorWidget->SetDescriptionSuffix(QString(m_Suffix.c_str()));
    }
  }
  else if (message.find("Server status: Server(s) are running.") != std::string::npos)
  {
    if (localConfigFile)
    {
      m_DeviceSetSelectorWidget->SetConnectionSuccessful(true);
      m_DeviceSetSelectorWidget->SetConnectButtonText(QString("Stop server"));
    }

    ShowNotification(QString("Configuration file: %1").arg(info.Filename.c_str()), "Server started");
  }
  else if (message.find("Server status: ") != std::string::npos)
  {
    if (localConfigFile)
    {
      // pull off server status and display it
      m_DeviceSetSelectorWidget->SetDescriptionSuffix(QString(message.c_str()));
    }
  }
}

//----------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::SendCommand(igtlioCommandPointer command)
{
  if (m_RemoteControlServerConnector->IsConnected() && m_RemoteControlServerConnector->SendCommand(command) == 1)
  {
    return PLUS_SUCCESS;
  }
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus PlusServerLauncherMainWindow::SendCommandResponse(igtlioCommandPointer command)
{
  if (m_RemoteControlServerConnector->SendCommandResponse(command))
  {
    return PLUS_SUCCESS;
  }
  LOG_ERROR("Unable to send command response to client: " << command);

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
    if (m_DeviceSetSelectorWidget->GetConnectionSuccessful())
    {
      m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
    }
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

  // If there is still an incomplete line, prepend it to the incoming string
  if (!m_LogIncompleteLine.empty())
  {
    logString = m_LogIncompleteLine + logString;
    m_LogIncompleteLine = "";
  }

  // If the last character in the incoming string is not a newline, then the string is incomplete
  bool logLineIncomplete = (logString.back() != '\n');

  StringList lines;
  if (logString.find('\n') != std::string::npos)
  {
    igsioCommon::SplitStringIntoTokens(logString, '\n', lines, false);
    for (StringList::iterator lineIt = lines.begin(); lineIt != lines.end(); ++lineIt)
    {
      if (igsioCommon::Trim(*lineIt).empty())
      {
        lines.erase(lineIt);
        lineIt = lines.begin();
      }

      // The last line in the string is incomplete
      // Store the characters so that they can be combined with the full line when it is received
      if (logLineIncomplete && lineIt == lines.end() - 1)
      {
        m_LogIncompleteLine = *lineIt;
        continue;
      }

      std::string line = *lineIt;
      StringList tokens;

      if (line.find('|') != std::string::npos)
      {
        igsioCommon::SplitStringIntoTokens(line, '|', tokens, false);
        // Remove empty tokens
        for (StringList::iterator it = tokens.begin(); it != tokens.end(); ++it)
        {
          if (igsioCommon::Trim(*it).empty())
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
            ParseContent(message);

            vtkPlusLogger::Instance()->LogMessage(logLevel, message.c_str(), file.c_str(), lineNumber, "SERVER");
          }
        }
      }
      else
      {
        vtkPlusLogger::Instance()->LogMessage(vtkPlusLogger::LOG_LEVEL_INFO, line.c_str(), "SERVER");
        ParseContent(line.c_str());
      }
    }
  }
  else
  {
    // No newline. String is incomplete
    m_LogIncompleteLine = logString;
  }
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::StdOutMsgReceived()
{
  QProcess* process = qobject_cast<QProcess*>(QObject::sender());
  if (!process)
  {
    return;
  }
  QByteArray strData = process->readAllStandardOutput();
  SendServerOutputToLogger(strData);
}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::StdErrMsgReceived()
{
  QProcess* process = qobject_cast<QProcess*>(QObject::sender());
  if (!process)
  {
    return;
  }
  QByteArray strData = process->readAllStandardError();
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

  QProcess* process = qobject_cast<QProcess*>(QObject::sender());
  if (process)
  {
    ServerInfo info = GetServerInfoFromProcess(process);
    if (info.Filename == m_LocalConfigFile)
    {
      m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
    }
  }


}

//-----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ServerExecutableFinished(int returnCode, QProcess::ExitStatus status)
{
  QProcess* finishedProcess = dynamic_cast<QProcess*>(sender());
  ServerInfo info = GetServerInfoFromProcess(finishedProcess);
  std::string configFileName = info.Filename;

  if (returnCode == 0)
  {
    LOG_INFO("Server process terminated.");
    ShowNotification(QString("Configuration file: %1").arg(configFileName.c_str()), "Server stopped");
  }
  else
  {
    LOG_ERROR("Server stopped unexpectedly. Return code: " << returnCode);
    ShowNotification(QString("Configuration file: %1").arg(configFileName.c_str()), "Server stopped unexpectedly");
  }

  if (finishedProcess)
  {
    RemoveServerProcess(finishedProcess);
  }

  if (strcmp(vtksys::SystemTools::GetFilenameName(m_LocalConfigFile).c_str(), configFileName.c_str()) == 0)
  {
    ConnectToDevicesByConfigFile("");
    ui.comboBox_LogLevel->setEnabled(true);
    if (m_DeviceSetSelectorWidget->GetConnectionSuccessful())
    {
      m_DeviceSetSelectorWidget->SetConnectionSuccessful(false);
    }
  }

  SendServerStoppedCommand(info);
  UpdateRemoteServerTable();
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::LogLevelChanged()
{
  vtkPlusLogger::Instance()->SetLogLevel(ui.comboBox_LogLevel->currentData().toInt());
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::LatestLogClicked()
{
  // Open the latest log in the output folder
  QDir directory(QString::fromStdString(vtkPlusConfig::GetInstance()->GetOutputDirectory()));
  QFileInfoList fileInfoList = directory.entryInfoList(QStringList() << "*.txt");
  if (fileInfoList.isEmpty())
  {
    LocalLog(vtkPlusLogger::LOG_LEVEL_INFO, "No logs in output directory.");
    return;
  }

  QFileInfo latestInfo = fileInfoList.at(0);
  QDateTime latestTime = fileInfoList.at(0).lastModified();
  for (auto& fileInfo : fileInfoList)
  {
    if (!fileInfo.isFile())
    {
      continue;
    }
    if (fileInfo.lastModified().secsTo(latestTime) < 0)
    {
      latestTime = fileInfo.lastModified();
      latestInfo = fileInfo;
    }
  }

  // Open it in an editor
  QString editorApplicationExecutable(vtkPlusConfig::GetInstance()->GetEditorApplicationExecutable().c_str());

  if (!editorApplicationExecutable.isEmpty())
  {
    QString file = latestInfo.absoluteFilePath();

    QProcess::startDetached(editorApplicationExecutable, QStringList() << file);
    return;
  }

  // No editor application defined, try using the system default
  if (!QDesktopServices::openUrl(QUrl("file:///" + latestInfo.absoluteFilePath(), QUrl::TolerantMode)))
  {
    LOG_ERROR("Failed to open file in default application: " << latestInfo.absoluteFilePath().toStdString());
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OpenLogFolderClicked()
{
  // Open the output folder
  QDir directory(QString::fromStdString(vtkPlusConfig::GetInstance()->GetOutputDirectory()));
  if (!QDesktopServices::openUrl(QUrl("file:///" + directory.absolutePath(), QUrl::TolerantMode)))
  {
    LOG_ERROR("Failed to open folder in default application: " << directory.absolutePath().toStdString());
  }
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientData, void* callData)
{
  PlusServerLauncherMainWindow* self = reinterpret_cast<PlusServerLauncherMainWindow*>(clientData);

  igtlioLogicPointer logic = igtlioLogic::SafeDownCast(caller);

  switch (eventId)
  {
  case igtlioConnector::ClientConnectedEvent:
  {
    self->LocalLog(vtkPlusLogger::LOG_LEVEL_INFO, "Client connected.");
    break;
  }
  case igtlioConnector::ClientDisconnectedEvent:
  {
    self->LocalLog(vtkPlusLogger::LOG_LEVEL_INFO, "Client disconnected.");
    self->OnClientDisconnectedEvent();
    break;
  }
  case igtlioCommand::CommandReceivedEvent:
  {
    if (logic == nullptr)
    {
      return;
    }
    igtlioCommandPointer command = reinterpret_cast<igtlioCommand*>(callData);
    self->OnCommandReceivedEvent(command);
    break;
  }
  case igtlioCommand::CommandResponseEvent:
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
void PlusServerLauncherMainWindow::OnClientDisconnectedEvent()
{
  std::queue<int> unsubscribedClients;

  std::vector<int> connectedClientIds = m_RemoteControlServerConnector->GetClientIds();
  for (std::set<int>::iterator subscribedClientIt = m_RemoteControlLogSubscribedClients.begin(); subscribedClientIt != m_RemoteControlLogSubscribedClients.end(); ++subscribedClientIt)
  {
    int clientId = *subscribedClientIt;
    std::vector<int>::iterator connectedClientIt = (std::find_if(
      connectedClientIds.begin(),
      connectedClientIds.end(),
      [this, clientId](const int& entry)
      {return entry == clientId; }));

    // Subscribed client is not connected. Remove from list of clients subscribed to log messages
    if (connectedClientIt == connectedClientIds.end())
    {
      unsubscribedClients.push(clientId);
    }
  }

  while (!unsubscribedClients.empty())
  {
    m_RemoteControlLogSubscribedClients.erase(unsubscribedClients.front());
    unsubscribedClients.pop();
  }
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::OnCommandReceivedEvent(igtlioCommandPointer command)
{
  if (!command)
  {
    LOG_ERROR("Command event could not be read!");
  }

  int id = command->GetCommandId();
  std::string name = command->GetName();

  LocalLog(vtkPlusLogger::LOG_LEVEL_DEBUG, std::string("Command \"") + name + "\" received.");

  if (igsioCommon::IsEqualInsensitive(name, "GetConfigFiles"))
  {
    GetConfigFiles(command);
    return;
  }
  else if (igsioCommon::IsEqualInsensitive(name, "AddConfigFile"))
  {
    AddOrUpdateConfigFile(command);
    return;
  }
  else if (igsioCommon::IsEqualInsensitive(name, "StartServer"))
  {
    RemoteStartServer(command);
    return;
  }
  else if (igsioCommon::IsEqualInsensitive(name, "StopServer"))
  {
    RemoteStopServer(command);
    return;
  }
  else if (igsioCommon::IsEqualInsensitive(name, "LogSubscribe"))
  {
    m_RemoteControlLogSubscribedClients.insert(command->GetClientId());
    return;
  }
  else if (igsioCommon::IsEqualInsensitive(name, "LogUnsubscribe"))
  {
    m_RemoteControlLogSubscribedClients.erase(command->GetClientId());
    return;
  }
  else if (igsioCommon::IsEqualInsensitive(name, "GetRunningServers"))
  {
    GetRunningServers(command);
    return;
  }
  else if (igsioCommon::IsEqualInsensitive(name, "GetConfigFileContents"))
  {
    GetConfigFileContents(command);
    return;
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::RemoteStopServer(igtlioCommandPointer command)
{
  IANA_ENCODING_TYPE encodingType = IANA_TYPE_US_ASCII;
  std::string filename;
  std::string id;
  if (!command->GetCommandMetaDataElement("ConfigFileName", filename, encodingType) &&
    !command->GetCommandMetaDataElement("ServerID", id, encodingType))
  {
    command->SetSuccessful(false);
    command->SetErrorMessage("Config file not specified.");
    if (SendCommandResponse(command) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }

  ServerInfo info;
  if (!id.empty())
  {
    info = GetServerInfoFromID(id);
    filename = info.Filename;
  }
  else if (!filename.empty())
  {
    info = GetServerInfoFromFilename(filename);
  }

  StopServer(QString::fromStdString(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(vtksys::SystemTools::GetFilenameName(filename))));

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> responseElement = vtkSmartPointer<vtkXMLDataElement>::New();
  responseElement->SetName("Response");
  responseElement->SetAttribute("ConfigFileName", filename.c_str());
  responseElement->SetAttribute("ServerID", id.c_str());
  commandElement->AddNestedElement(responseElement);

  std::stringstream commandSS;
  vtkXMLUtilities::FlattenElement(commandElement, commandSS);

  // Forced stop or not, the server is down
  command->SetSuccessful(true);
  command->SetResponseMetaDataElement("ConfigFileName", filename);
  command->SetResponseMetaDataElement("ServerID", id);
  if (SendCommandResponse(command) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }
  return;
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::RemoteStartServer(igtlioCommandPointer command)
{
  IANA_ENCODING_TYPE encodingType = IANA_TYPE_US_ASCII;
  std::string filename;
  if (!command->GetCommandMetaDataElement("ConfigFileName", filename, encodingType))
  {
    igtl::MessageBase::MetaDataMap map;
    command->SetSuccessful(false);
    command->SetErrorMessage("Config file not specified.");
    if (SendCommandResponse(command) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }

  // TODO: Start server will not reduce the log level of the server below LOG_INFO for remotely started servers
  // Should probably solve by separating servers according to their origin and handle each separately
  int logLevel = -1;
  std::string logLevelString = "";
  command->GetCommandMetaDataElement("LogLevel", logLevelString, encodingType);
  if (igsioCommon::StringToInt<int>(logLevelString.c_str(), logLevel) == PLUS_FAIL)
  {
    logLevel = vtkPlusLogger::LOG_LEVEL_INFO;
  }

  if (!StartServer(QString::fromStdString(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(vtksys::SystemTools::GetFilenameName(filename))), logLevel))
  {
    command->SetSuccessful(false);
    command->SetErrorMessage("Failed to start server process.");
    if (SendCommandResponse(command) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }
    return;
  }

  std::string servers = GetServersFromConfigFile(filename);
  std::stringstream responseSS;
  vtkSmartPointer<vtkXMLDataElement> responseXML = vtkSmartPointer<vtkXMLDataElement>::New();
  responseXML->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> resultXML = vtkSmartPointer<vtkXMLDataElement>::New();
  resultXML->SetName("Result");
  resultXML->SetAttribute("ConfigFileName", filename.c_str());
  resultXML->SetAttribute("Servers", servers.c_str());
  responseXML->AddNestedElement(resultXML);
  vtkXMLUtilities::FlattenElement(responseXML, responseSS);

  command->SetSuccessful(true);
  command->SetResponseMetaDataElement("ConfigFileName", filename);
  command->SetResponseMetaDataElement("Servers", servers);
  if (SendCommandResponse(command) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }

}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::GetConfigFiles(igtlioCommandPointer command)
{
  vtkSmartPointer<vtkDirectory> dir = vtkSmartPointer<vtkDirectory>::New();
  if (dir->Open(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory().c_str()) == 0)
  {
    command->SetSuccessful(false);
    command->SetErrorMessage("Unable to open device set directory.");
    if (SendCommandResponse(command) != PLUS_SUCCESS)
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
    if (igsioCommon::IsEqualInsensitive(ext, ".xml"))
    {
      ss << file << ";";
    }
  }

  command->SetSuccessful(true);
  command->SetResponseMetaDataElement("ConfigFiles", ss.str());
  command->SetResponseMetaDataElement("Separator", ";");
  if (SendCommandResponse(command) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::GetRunningServers(igtlioCommandPointer command)
{
  std::stringstream ss;
  std::deque<ServerInfo> runningServers = m_ServerInstances;
  for (std::deque<ServerInfo>::iterator serverIt = runningServers.begin(); serverIt != runningServers.end(); ++serverIt)
  {
    ss << serverIt->ID << ";";
  }

  command->SetSuccessful(true);
  command->SetResponseMetaDataElement("RunningServers", ss.str());
  command->SetResponseMetaDataElement("Separator", ";");
  if (SendCommandResponse(command) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }
  return;
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::GetConfigFileContents(igtlioCommandPointer command)
{
  IANA_ENCODING_TYPE encodingType = IANA_TYPE_US_ASCII;
  std::string serverIdsString;
  command->GetCommandMetaDataElement("ServerIDs", serverIdsString, encodingType);
  std::string separator;
  command->GetCommandMetaDataElement("Separator", separator, encodingType);

  std::vector<std::string> serverIds;
  if (!separator.empty() && !serverIdsString.empty())
  {
    serverIds = igsioCommon::SplitStringIntoTokens(serverIdsString, separator.c_str()[0], false);
  }

  vtkSmartPointer<vtkXMLDataElement> rootElement = vtkSmartPointer<vtkXMLDataElement>::New();
  rootElement->SetName("Command");

  for (std::string serverId : serverIds)
  {
    ServerInfo info = GetServerInfoFromID(serverId);
    if (!info.Process)
    {
      continue;
    }

    vtkSmartPointer<vtkXMLDataElement> configFileElement = vtkSmartPointer<vtkXMLDataElement>::New();
    configFileElement->SetName(serverId.c_str());

    std::string filePath = vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationPath(vtksys::SystemTools::GetFilenameName(info.Filename));
    vtkSmartPointer<vtkXMLDataElement> contentElement = vtkXMLUtilities::ReadElementFromFile(filePath.c_str());

    configFileElement->AddNestedElement(contentElement);
    rootElement->AddNestedElement(configFileElement);
  }

  std::stringstream ss;
  vtkXMLUtilities::FlattenElement(rootElement, ss);
  command->SetResponseContent(ss.str());
  command->SetSuccessful(true);
  if (SendCommandResponse(command) != PLUS_SUCCESS)
  {
    LOG_ERROR("Command received but response could not be sent.");
  }
  return;
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::LocalLog(vtkPlusLogger::LogLevelType level, const std::string& message)
{
  statusBar()->showMessage(QString::fromStdString(message));
  LOG_DYNAMIC(message, level);
}

//----------------------------------------------------------------------------
void PlusServerLauncherMainWindow::AddOrUpdateConfigFile(igtlioCommandPointer command)
{
  IANA_ENCODING_TYPE encodingType = IANA_TYPE_US_ASCII;
  std::string configFile;
  bool hasFilename = command->GetCommandMetaDataElement("ConfigFileName", configFile, encodingType);
  std::string configFileContent;
  bool hasFileContent = command->GetCommandMetaDataElement("ConfigFileContent", configFileContent, encodingType);

  // Check write permissions
  if (!ui.checkBox_writePermission->isChecked())
  {
    LOG_INFO("Request from client to add config file, but write permissions not enabled. File: " << (hasFilename ? configFile : "unknown"));

    command->SetSuccessful(false);
    command->SetErrorMessage("Write permission denied.");
    if (SendCommandResponse(command) != PLUS_SUCCESS)
    {
      LOG_ERROR("Command received but response could not be sent.");
    }

    return;
  }

  if (!hasFilename || !hasFileContent)
  {
    command->SetSuccessful(false);
    command->SetErrorMessage("Required metadata \'ConfigFileName\' and/or \'ConfigFileContent\' missing.");
    if (SendCommandResponse(command) != PLUS_SUCCESS)
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
        configFile = oldConfigFile + "[" + igsioCommon::ToString<int>(i) + "]";
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

    command->SetSuccessful(false);
    command->SetErrorMessage("Unable to write to device set configuration directory.");
    if (SendCommandResponse(command) != PLUS_SUCCESS)
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

  command->SetSuccessful(true);
  command->SetResponseMetaDataElement("ConfigFileName", configFile);
  if (SendCommandResponse(command) != PLUS_SUCCESS)
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
void PlusServerLauncherMainWindow::SendServerStartedCommand(ServerInfo serverInfo)
{
  LOG_TRACE("Sending server started command");

  std::string logLevelString = igsioCommon::ToString(ui.comboBox_LogLevel->currentData().Int);

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> startServerElement = vtkSmartPointer<vtkXMLDataElement>::New();
  startServerElement->SetName("ServerStarted");
  startServerElement->SetAttribute("LogLevel", logLevelString.c_str());
  startServerElement->SetAttribute("ConfigFileName", serverInfo.Filename.c_str());
  startServerElement->SetAttribute("ServerID", serverInfo.ID.c_str());
  commandElement->AddNestedElement(startServerElement);

  std::stringstream commandStream;
  vtkXMLUtilities::FlattenElement(commandElement, commandStream);

  igtlioCommandPointer serverStartedCommand = igtlioCommandPointer::New();
  serverStartedCommand->BlockingOff();
  serverStartedCommand->SetName("ServerStarted");
  serverStartedCommand->SetCommandContent(commandStream.str());
  serverStartedCommand->SetCommandMetaDataElement("LogLevel", startServerElement->GetAttribute("LogLevel"));
  serverStartedCommand->SetCommandMetaDataElement("ConfigFileName", serverInfo.Filename);
  serverStartedCommand->SetCommandMetaDataElement("ServerID", serverInfo.ID);

  SendCommand(serverStartedCommand);
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::SendServerStoppedCommand(ServerInfo info)
{
  LOG_TRACE("Sending server stopped command");

  vtkSmartPointer<vtkXMLDataElement> commandElement = vtkSmartPointer<vtkXMLDataElement>::New();
  commandElement->SetName("Command");
  vtkSmartPointer<vtkXMLDataElement> serverStoppedElement = vtkSmartPointer<vtkXMLDataElement>::New();
  serverStoppedElement->SetName("ServerStopped");
  serverStoppedElement->SetAttribute("ConfigFileName", info.Filename.c_str());
  commandElement->AddNestedElement(serverStoppedElement);

  std::stringstream commandStream;
  vtkXMLUtilities::FlattenElement(commandElement, commandStream);

  igtlioCommandPointer serverStoppedCommand = igtlioCommandPointer::New();
  serverStoppedCommand->BlockingOff();
  serverStoppedCommand->SetName("ServerStopped");
  serverStoppedCommand->SetCommandContent(commandStream.str());
  serverStoppedCommand->SetCommandMetaDataElement("ConfigFileName", info.Filename);

  SendCommand(serverStoppedCommand);
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
  if (self->m_RemoteControlLogSubscribedClients.empty())
  {
    return;
  }

  // We don't want to end up in an infinite loop of logging if something goes wrong, so remove the log observer
  vtkPlusLogger::Instance()->RemoveObserver(self->m_RemoteControlLogMessageCallbackCommand);

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

  if (!logMessage.isEmpty())
  {
#if (QT_VERSION >= QT_VERSION_CHECK(5,14,0))
    QStringList tokens = logMessage.split('|', Qt::SkipEmptyParts);
#else
    QStringList tokens = logMessage.split('|', QString::SkipEmptyParts);
#endif
    if (tokens.size() > 0)
    {
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

      for (std::set<int>::iterator subscribedClientsIt = self->m_RemoteControlLogSubscribedClients.begin(); subscribedClientsIt != self->m_RemoteControlLogSubscribedClients.end(); ++subscribedClientsIt)
      {
        igtlioCommandPointer logMessageCommand = igtlioCommandPointer::New();
        logMessageCommand->SetClientId(*subscribedClientsIt);
        logMessageCommand->BlockingOff();
        logMessageCommand->SetName("LogMessage");
        logMessageCommand->SetCommandContent(messageCommand.str());
        logMessageCommand->SetCommandMetaDataElement("Message", logMessageString);
        logMessageCommand->SetCommandMetaDataElement("LogLevel", logLevel);
        logMessageCommand->SetCommandMetaDataElement("Origin", messageOrigin);
        self->SendCommand(logMessageCommand);
      }
    }
  }

  // Re-apply the log observers
  vtkPlusLogger::Instance()->AddObserver(vtkPlusLogger::MessageLogged, self->m_RemoteControlLogMessageCallbackCommand);
  vtkPlusLogger::Instance()->AddObserver(vtkPlusLogger::WideMessageLogged, self->m_RemoteControlLogMessageCallbackCommand);
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

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::closeEvent(QCloseEvent* event)
{
  QApplication* app = qobject_cast<QApplication*>(QApplication::instance());
  app->setQuitOnLastWindowClosed(true);

  if (!event->spontaneous() || !isVisible())
  {
    return;
  }

  if (ui.checkBox_minimizeOnClose->isChecked())
  {
    app->setQuitOnLastWindowClosed(false);
    QMessageBox::information(this, tr("Plus Server Launcher"),
      tr("The program will keep running in the "
        "system tray. To terminate the program, "
        "right-click on the system tray icon and "
        "choose <b>Exit</b>."));
  }
}

//-----------------------------------------------------------------------------
bool PlusServerLauncherMainWindow::GetHideOnStartup() const
{
  return ui.checkBox_startMinimized->isChecked();
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::ShowNotification(QString message, QString title)
{
  if (!ui.checkBox_showNotifications->isChecked())
  {
    return;
  }

#if (QT_VERSION >= QT_VERSION_CHECK(5,9,0))
  m_SystemTrayIcon->showMessage(
    title,
    message,
    m_SystemTrayIcon->icon(),
    SYSTEM_TRAY_MESSAGE_TIMEOUT_MS);
#else
  m_SystemTrayIcon->showMessage(
    title,
    message,
    QSystemTrayIcon::Information,
    SYSTEM_TRAY_MESSAGE_TIMEOUT_MS);
#endif
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::SystemTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
  if (reason == QSystemTrayIcon::ActivationReason::Context)
  {
    m_SystemTrayShowAction->setVisible(!isVisible());
    m_SystemTrayHideAction->setVisible(isVisible());
  }
  else
  {
    show();
    raise();
    activateWindow();
  }
}

//---------------------------------------------------------------------------
void PlusServerLauncherMainWindow::SystemTrayMessageClicked()
{
  show();
  raise();
  activateWindow();
}
