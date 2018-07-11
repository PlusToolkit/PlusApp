/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#ifndef __PlusServerLauncherMainWindow_h
#define __PlusServerLauncherMainWindow_h

#include "PlusConfigure.h"
#include "ui_PlusServerLauncherMainWindow.h"

#include <QMainWindow>
#include <QProcess>

// OpenIGTLinkIO includes
#include <igtlioCommand.h>
#include <igtlioConnector.h>
#include <igtlioLogic.h>

class QComboBox;
class QPlusDeviceSetSelectorWidget;
class QProcess;
class QTimer;
class QWidget;
class vtkPlusDataCollector;
class vtkPlusOpenIGTLinkServer;
class vtkPlusTransformRepository;

//-----------------------------------------------------------------------------

/*!
  \class PlusServerLauncherMainWindow
  \brief GUI application for starting an OpenIGTLink server with the selected device configuration file
  \ingroup PlusAppPlusServerLauncher
 */
class PlusServerLauncherMainWindow : public QMainWindow
{
  Q_OBJECT

public:
  enum
  {
    RemoteControlServerPortDisable = -1,
    RemoteControlServerPortUseDefault = 0
  };
  static const int DEFAULT_REMOTE_CONTROL_SERVER_PORT = 18904;
  static const char* PLUS_SERVER_LAUNCHER_REMOTE_DEVICE_ID;

  /*!
    Constructor
    \param aParent parent
    \param aFlags widget flag
    \param remoteControlServerPort port number where launcher listens for remote control OpenIGTLink commands. 0 means use default port, -1 means do not start a remote control server.
  */
  PlusServerLauncherMainWindow(QWidget* parent = 0, Qt::WindowFlags flags = 0, bool autoConnect = false, int remoteControlServerPort = RemoteControlServerPortUseDefault);
  ~PlusServerLauncherMainWindow();

protected slots:
  /*!
    Connect to devices described in the argument configuration file in response by clicking on the Connect button
    \param aConfigFile DeviceSet configuration file path and name
  */
  void ConnectToDevicesByConfigFile(std::string);

  /*! Called whenever a key is pressed while the windows is active, used for intercepting the ESC key */
  virtual void keyPressEvent(QKeyEvent* e);

  void StdOutMsgReceived();

  void StdErrMsgReceived();

  void ErrorReceived(QProcess::ProcessError);

  void ServerExecutableFinished(int returnCode, QProcess::ExitStatus status);

  void LogLevelChanged();

  static void OnRemoteControlServerEventReceived(vtkObject* caller, unsigned long eventId, void* clientdata, void* calldata);
  void OnClientDisconnectedEvent();
  void OnCommandReceivedEvent(igtlioCommandPointer command);
  static void OnLogEvent(vtkObject* caller, unsigned long eventId, void* clientData, void* callData);

  void OnWritePermissionClicked();

  void OnTimerTimeout();

protected:
  /*! Receive standard output or error and send it to the log */
  void SendServerOutputToLogger(const QByteArray& strData);

  /*! Start server process, connect outputs to logger. Returns with true on success. */
  bool StartServer(const QString& configFilePath, int logLevel = vtkPlusLogger::LOG_LEVEL_UNDEFINED);
  /*! Start server process from GUI */
  bool LocalStartServer();

  /*! Stop server process, disconnect outputs. Returns with true on success (shutdown on request was successful, without forcing). */
  bool StopServer(const QString& configFilePath);
  bool LocalStopServer();

  /*! Parse a given log line for salient information from the PlusServer */
  void ParseContent(const std::string& message);

  /*! Send a command */
  PlusStatus SendCommand(igtlioCommandPointer command);

  /*! Send a response to a command */
  PlusStatus SendCommandResponse(igtlioCommandPointer command);

  /*! Incoming command handling functions */
  void AddOrUpdateConfigFile(igtlioCommandPointer clientDevice);
  void GetConfigFiles(igtlioCommandPointer clientDevice);
  void RemoteStartServer(igtlioCommandPointer clientDevice);
  void RemoteStopServer(igtlioCommandPointer clientDevice);

  void LocalLog(vtkPlusLogger::LogLevelType level, const std::string& message);

  std::string GetServersFromConfigFile(std::string filename);

  void SendServerStartedCommand(std::string configFilePath);
  void SendServerStoppedCommand(std::string configFilePath);

protected:
  /*! Device set selector widget */
  QPlusDeviceSetSelectorWidget*         m_DeviceSetSelectorWidget;

  /*! PlusServer instance that is responsible for all data collection and network transfer */
  std::map<std::string, QProcess*>      m_ServerInstances;

  /*! List of active ports for PlusServers */
  std::string                           m_Suffix;

  /*! Store local config file name */
  std::string                           m_LocalConfigFile;

  /*! OpenIGTLink server that allows remote control of launcher (start/stop a PlusServer process, etc) */
  int m_RemoteControlServerPort;
  vtkSmartPointer<vtkCallbackCommand>   m_RemoteControlServerCallbackCommand;
  igtlioLogicPointer                    m_RemoteControlServerLogic;
  igtlioConnectorPointer                m_RemoteControlServerConnector;
  vtkSmartPointer<vtkCallbackCommand>   m_RemoteControlLogMessageCallbackCommand;

  QTimer*                               m_RemoteControlServerConnectorProcessTimer;

  std::set<int>                         m_RemoteControlLogSubscribedClients;

private:
  Ui::PlusServerLauncherMainWindow ui;
};

#endif // __PlusServerLauncherMainWindow_h
