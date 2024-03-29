/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "QStylusCalibrationToolbox.h"
#include "fCalMainWindow.h"
#include "igsioMath.h"
#include "vtkPlusPivotCalibrationAlgo.h"
#include "vtkPlusVisualizationController.h"

// Qt includes
#include <QFileDialog>
#include <QTimer>

// VTK includes
#include <vtkMatrix4x4.h>
#include <vtkPoints.h>
#include <vtkRenderer.h>

//-----------------------------------------------------------------------------
QStylusCalibrationToolbox::QStylusCalibrationToolbox(fCalMainWindow* aParentMainWindow, Qt::WindowFlags aFlags)
  : QAbstractToolbox(aParentMainWindow)
  , QWidget(aParentMainWindow, aFlags)
  , m_PivotCalibration(vtkSmartPointer<vtkPlusPivotCalibrationAlgo>::New())
  , m_NumberOfPoints(200)
  , m_FreeHandStartupDelaySec(5)
  , m_CurrentPointNumber(0)
  , m_PreviousStylusToReferenceTransformMatrix(vtkSmartPointer<vtkMatrix4x4>::New())
{
  ui.setupUi(this);

  // Feed number of points from controller
  ui.spinBox_NumberOfStylusCalibrationPoints->setValue(m_NumberOfPoints);

  // Connect events
  connect(ui.pushButton_StartStop, SIGNAL(clicked()), this, SLOT(OnStartStopClicked()));
  connect(ui.spinBox_NumberOfStylusCalibrationPoints, SIGNAL(valueChanged(int)), this, SLOT(NumberOfStylusCalibrationPointsChanged(int)));
}

//-----------------------------------------------------------------------------
QStylusCalibrationToolbox::~QStylusCalibrationToolbox()
{
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::OnActivated()
{
  LOG_TRACE("StylusCalibrationToolbox::OnActivated");

  if (m_State != ToolboxState_Done)
  {
    // Clear results polydata
    m_ParentMainWindow->GetVisualizationController()->ClearResultPolyData();

    bool initializationSuccess = true;
    if ((m_ParentMainWindow->GetVisualizationController()->GetDataCollector() == NULL) || !(m_ParentMainWindow->GetVisualizationController()->GetDataCollector()->GetConnected()))
    {
      LOG_ERROR("Reading pivot calibration algorithm configuration failed!");
      initializationSuccess = false;
    }
    if (initializationSuccess && m_PivotCalibration->ReadConfiguration(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData()) != PLUS_SUCCESS)
    {
      LOG_ERROR("Reading pivot calibration algorithm configuration failed!");
      initializationSuccess = false;
    }
    if (initializationSuccess && ReadConfiguration(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData()) != PLUS_SUCCESS)
    {
      LOG_ERROR("Reading stylus calibration configuration failed!");
      initializationSuccess = false;
    }
    // Check if stylus to reference transform is available
    std::string objectMarkerCoordinateFrame = m_PivotCalibration->GetObjectMarkerCoordinateFrame();
    std::string referenceCoordinateFrame = m_PivotCalibration->GetReferenceCoordinateFrame();
    if (initializationSuccess && m_ParentMainWindow->GetVisualizationController()->IsExistingTransform(
      objectMarkerCoordinateFrame.c_str(), referenceCoordinateFrame.c_str()) != PLUS_SUCCESS)
    {
      LOG_ERROR("No transform found between stylus and reference!");
      initializationSuccess = false;
    }

    if (initializationSuccess)
    {
      SetState(ToolboxState_Idle);
    }
    else
    {
      SetState(ToolboxState_Uninitialized);
    }
  }
  SetDisplayAccordingToState();
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::OnDeactivated()
{
  if (m_State == ToolboxState_StartupDelay || m_State == ToolboxState_InProgress)
  {
    StopCalibration();
  }
}

//-----------------------------------------------------------------------------
PlusStatus QStylusCalibrationToolbox::ReadConfiguration(vtkXMLDataElement* aConfig)
{
  LOG_TRACE("StylusCalibrationToolbox::ReadConfiguration");

  if (aConfig == NULL)
  {
    LOG_ERROR("Unable to read configuration");
    return PLUS_FAIL;
  }

  vtkXMLDataElement* fCalElement = aConfig->FindNestedElementWithName("fCal");
  if (fCalElement == NULL)
  {
    LOG_ERROR("Unable to find fCal element in XML tree!");
    return PLUS_FAIL;
  }

  // Number of stylus calibration points to acquire
  int numberOfStylusCalibrationPointsToAcquire = 0;
  if (fCalElement->GetScalarAttribute("NumberOfStylusCalibrationPointsToAcquire", numberOfStylusCalibrationPointsToAcquire))
  {
    m_NumberOfPoints = numberOfStylusCalibrationPointsToAcquire;
    ui.spinBox_NumberOfStylusCalibrationPoints->setValue(m_NumberOfPoints);
  }
  else
  {
    LOG_WARNING("Unable to read NumberOfStylusCalibrationPointsToAcquire attribute from fCal element of the device set configuration, default value '" << m_NumberOfPoints << "' will be used");
  }

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, FreeHandStartupDelaySec, fCalElement);

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::RefreshContent()
{
  LOG_TRACE("StylusCalibrationToolbox::RefreshContent");

  if (m_State == ToolboxState_Idle)
  {
    ui.label_NumberOfPoints->setText(QString("%1 / %2").arg(0).arg(m_NumberOfPoints));
    ui.label_CurrentPosition->setText(m_StylusPositionString);
  }
  else if (m_State == ToolboxState_StartupDelay)
  {
    int startupDelayRemainingTimeSec = m_FreeHandStartupDelaySec - 0.001 * m_CalibrationStartupDelayStartTime.elapsed();
    if (startupDelayRemainingTimeSec < 0)
    {
      startupDelayRemainingTimeSec = 0;
    }
    ui.label_Instructions->setText(QString("Stylus positions recording will start in %1").arg(startupDelayRemainingTimeSec));
    ui.label_NumberOfPoints->setText(QString("%1 / %2").arg(0).arg(m_NumberOfPoints));
    ui.label_CurrentPosition->setText(m_StylusPositionString);
  }
  else if (m_State == ToolboxState_InProgress)
  {
    ui.label_NumberOfPoints->setText(QString("%1 / %2").arg(m_CurrentPointNumber).arg(m_NumberOfPoints));
    ui.label_CurrentPosition->setText(m_StylusPositionString);
    m_ParentMainWindow->SetStatusBarProgress((int)(100.0 * (m_CurrentPointNumber / (double)m_NumberOfPoints) + 0.5));
  }
  else if (m_State == ToolboxState_Done)
  {
    // Get stylus tip position and display it
    std::string stylusTipPosition;
    ToolStatus status(TOOL_INVALID);
    std::string objectPivotPointCoordinateFrame = m_PivotCalibration->GetObjectPivotPointCoordinateFrame();
    std::string referenceCoordinateFrame = m_PivotCalibration->GetReferenceCoordinateFrame();
    if (m_ParentMainWindow->GetVisualizationController()->GetTransformTranslationString(objectPivotPointCoordinateFrame.c_str(), referenceCoordinateFrame.c_str(), stylusTipPosition, &status) != PLUS_SUCCESS)
    {
      LOG_ERROR("Unable to get stylus tip to reference transform!");
      return;
    }

    if (status == TOOL_OK)
    {
      ui.label_CurrentPosition->setText(QString(stylusTipPosition.c_str()));
    }
    else
    {
      ui.label_CurrentPosition->setText(tr("Stylus is out of view"));
    }
  }
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::SetDisplayAccordingToState()
{
  LOG_TRACE("StylusCalibrationToolbox::SetDisplayAccordingToState");

  // If connected
  if ((m_ParentMainWindow->GetVisualizationController()->GetDataCollector() != NULL)
      && (m_ParentMainWindow->GetVisualizationController()->GetDataCollector()->GetConnected()))
  {
    // If the force show devices isn't enabled, set it to 3D and hide all the devices
    // Later, we will re-enable only those that we wish shown for this toolbox
    if (!m_ParentMainWindow->IsForceShowDevicesEnabled())
    {
      m_ParentMainWindow->GetVisualizationController()->SetVisualizationMode(vtkPlusVisualizationController::DISPLAY_MODE_3D);
      m_ParentMainWindow->GetVisualizationController()->HideAll();
    }

    // Enable or disable the image manipulation menu
    m_ParentMainWindow->SetImageManipulationMenuEnabled(m_ParentMainWindow->GetVisualizationController()->Is2DMode());

    std::string objectPivotPointCoordinateFrame = m_PivotCalibration->GetObjectPivotPointCoordinateFrame();
    std::string objectMarkerCoordinateFrame = m_PivotCalibration->GetObjectMarkerCoordinateFrame();

    // Update state message according to available transforms
    if (!objectPivotPointCoordinateFrame.empty() && !objectMarkerCoordinateFrame.empty())
    {
      std::string stylusTipToStylusTransformNameStr;
      igsioTransformName stylusTipToStylusTransformName(
        m_PivotCalibration->GetObjectPivotPointCoordinateFrame(), m_PivotCalibration->GetObjectMarkerCoordinateFrame());
      stylusTipToStylusTransformName.GetTransformName(stylusTipToStylusTransformNameStr);

      if (m_ParentMainWindow->GetVisualizationController()->IsExistingTransform(
            objectPivotPointCoordinateFrame.c_str(), objectMarkerCoordinateFrame.c_str(), false) == PLUS_SUCCESS)
      {
        std::string date, errorStr;
        double error;
        if (m_ParentMainWindow->GetVisualizationController()->GetTransformRepository()->GetTransformDate(stylusTipToStylusTransformName, date) != PLUS_SUCCESS)
        {
          date = "N/A";
        }
        if (m_ParentMainWindow->GetVisualizationController()->GetTransformRepository()->GetTransformError(stylusTipToStylusTransformName, error) == PLUS_SUCCESS)
        {
          std::stringstream ss;
          ss << std::fixed << error;
          errorStr = ss.str();
        }
        else
        {
          errorStr = "N/A";
        }

        QPalette palette;
        palette.setColor(ui.label_State->foregroundRole(), Qt::black);
        ui.label_State->setPalette(palette);
        ui.label_State->setText(QString("%1 transform present.\nDate: %2, Error: %3").arg(stylusTipToStylusTransformNameStr.c_str()).arg(date.c_str()).arg(errorStr.c_str()));
      }
      else
      {
        QPalette palette;
        palette.setColor(ui.label_State->foregroundRole(), QColor::fromRgb(255, 128, 0));
        ui.label_State->setPalette(palette);
        ui.label_State->setText(QString("%1 transform is absent, calibration needs to be performed.").arg(stylusTipToStylusTransformNameStr.c_str()));
        LOG_DEBUG(stylusTipToStylusTransformNameStr << " transform is absent, calibration needs to be performed.");
      }
    }
    else
    {
      QPalette palette;
      palette.setColor(ui.label_State->foregroundRole(), QColor::fromRgb(255, 128, 0));
      ui.label_State->setPalette(palette);
      ui.label_State->setText(QString("Stylus calibration configuration is missing!"));
      LOG_INFO("Stylus calibration configuration is missing");
      m_State = ToolboxState_Error;
    }
  }
  else
  {
    QPalette palette;
    palette.setColor(ui.label_State->foregroundRole(), QColor::fromRgb(255, 128, 0));
    ui.label_State->setPalette(palette);
    ui.label_State->setText(tr("fCal is not connected to devices. Switch to Configuration toolbox to connect."));
    LOG_INFO("fCal is not connected to devices");
    m_State = ToolboxState_Error;
  }

  // Set widget states according to state
  if (m_State == ToolboxState_Uninitialized)
  {
    SetBusyCursor(false);
    m_ParentMainWindow->SetToolboxesEnabled(true);
    disconnect(&m_ParentMainWindow->GetVisualizationController()->GetAcquisitionTimer(), &QTimer::timeout, this, &QStylusCalibrationToolbox::OnDataAcquired);

    ui.label_NumberOfPoints->setText(QString("%1 / %2").arg(0).arg(m_NumberOfPoints));
    ui.label_CalibrationError->setText(tr("N/A"));
    ui.label_CurrentPosition->setText(tr("N/A"));
    ui.label_StylusTipTransform->setText(tr("N/A"));
    ui.label_Instructions->setText(tr(""));

    ui.pushButton_StartStop->setEnabled(false);
    ui.pushButton_StartStop->setText("Start");
    ui.spinBox_NumberOfStylusCalibrationPoints->setEnabled(false);

    m_ParentMainWindow->SetStatusBarText(QString(""));
    m_ParentMainWindow->SetStatusBarProgress(-1);
  }
  else if (m_State == ToolboxState_Idle)
  {
    SetBusyCursor(false);
    m_ParentMainWindow->SetToolboxesEnabled(true);
    disconnect(&m_ParentMainWindow->GetVisualizationController()->GetAcquisitionTimer(), &QTimer::timeout, this, &QStylusCalibrationToolbox::OnDataAcquired);

    ui.label_CalibrationError->setText(tr("N/A"));
    ui.label_CurrentPositionText->setText(tr("Current stylus position (mm):"));
    ui.label_CurrentPosition->setText(tr("N/A"));
    ui.label_StylusTipTransform->setText(tr("N/A"));
    ui.label_Instructions->setText(tr("Put stylus so that its tip is in steady position, and press Start"));

    ui.pushButton_StartStop->setEnabled(true);
    ui.pushButton_StartStop->setText("Start");
    ui.spinBox_NumberOfStylusCalibrationPoints->setEnabled(true);

    ui.pushButton_StartStop->setFocus();

    m_ParentMainWindow->SetStatusBarText(QString(""));
    m_ParentMainWindow->SetStatusBarProgress(-1);
  }
  else if (m_State == ToolboxState_StartupDelay)
  {
    SetBusyCursor(true);
    m_ParentMainWindow->SetToolboxesEnabled(false);
    connect(&m_ParentMainWindow->GetVisualizationController()->GetAcquisitionTimer(), &QTimer::timeout, this, &QStylusCalibrationToolbox::OnDataAcquired);

    ui.label_CalibrationError->setText(tr("N/A"));
    ui.label_CurrentPositionText->setText(tr("Current stylus position (mm):"));
    ui.label_CurrentPosition->setText(tr("N/A"));
    ui.label_StylusTipTransform->setText(tr("N/A"));

    ui.pushButton_StartStop->setEnabled(true);
    ui.pushButton_StartStop->setText("Stop");
    ui.spinBox_NumberOfStylusCalibrationPoints->setEnabled(true);

    m_ParentMainWindow->SetStatusBarText(QString("Get ready to record stylus positions"));
    m_ParentMainWindow->SetStatusBarProgress(-1);
  }
  else if (m_State == ToolboxState_InProgress)
  {
    SetBusyCursor(true);
    m_ParentMainWindow->SetToolboxesEnabled(false);
    connect(&m_ParentMainWindow->GetVisualizationController()->GetAcquisitionTimer(), &QTimer::timeout, this, &QStylusCalibrationToolbox::OnDataAcquired);

    ui.label_NumberOfPoints->setText(QString("%1 / %2").arg(m_CurrentPointNumber).arg(m_NumberOfPoints));
    ui.label_CalibrationError->setText(tr("N/A"));
    ui.label_CurrentPositionText->setText(tr("Current stylus position (mm):"));
    ui.label_CurrentPosition->setText(m_StylusPositionString);
    ui.label_StylusTipTransform->setText(tr("N/A"));
    ui.label_Instructions->setText(tr("Move around stylus with its tip fixed until the required amount of points are acquired"));

    ui.pushButton_StartStop->setEnabled(true);
    ui.pushButton_StartStop->setText("Stop");
    ui.spinBox_NumberOfStylusCalibrationPoints->setEnabled(false);

    m_ParentMainWindow->SetStatusBarText(QString(" Recording stylus positions"));
    m_ParentMainWindow->SetStatusBarProgress(0);

    m_ParentMainWindow->GetVisualizationController()->ShowInput(true);
  }
  else if (m_State == ToolboxState_Done)
  {
    SetBusyCursor(false);
    m_ParentMainWindow->SetToolboxesEnabled(true);
    disconnect(&m_ParentMainWindow->GetVisualizationController()->GetAcquisitionTimer(), &QTimer::timeout, this, &QStylusCalibrationToolbox::OnDataAcquired);

    ui.label_Instructions->setText(tr("Calibration transform is ready to save"));

    ui.pushButton_StartStop->setEnabled(true);
    ui.pushButton_StartStop->setText("Start");
    ui.spinBox_NumberOfStylusCalibrationPoints->setEnabled(true);

    std::string pivotPointToMarkerTranslationString = m_PivotCalibration->GetPivotPointToMarkerTranslationString();
    ui.label_NumberOfPoints->setText(QString("%1 / %2").arg(m_CurrentPointNumber).arg(m_NumberOfPoints));
    ui.label_CalibrationError->setText(QString("%1 mm").arg(m_PivotCalibration->GetPivotCalibrationErrorMm(), 2));
    ui.label_CurrentPositionText->setText(tr("Current stylus tip position (mm):"));
    ui.label_CurrentPosition->setText(m_StylusPositionString);
    ui.label_StylusTipTransform->setText(pivotPointToMarkerTranslationString.c_str());

    m_ParentMainWindow->SetStatusBarText(QString(" Stylus calibration done"));
    m_ParentMainWindow->SetStatusBarProgress(-1);

    m_ParentMainWindow->GetVisualizationController()->ShowInput(true);
    m_ParentMainWindow->GetVisualizationController()->ShowResult(true);
    m_ParentMainWindow->GetVisualizationController()->ResetCamera();
    m_ParentMainWindow->GetVisualizationController()->ShowObjectById(m_ParentMainWindow->GetStylusModelId(), true);
  }
  else if (m_State == ToolboxState_Error)
  {
    SetBusyCursor(false);
    m_ParentMainWindow->SetToolboxesEnabled(true);
    disconnect(&m_ParentMainWindow->GetVisualizationController()->GetAcquisitionTimer(), &QTimer::timeout, this, &QStylusCalibrationToolbox::OnDataAcquired);

    ui.label_Instructions->setText(tr(""));

    ui.pushButton_StartStop->setEnabled(false);
    ui.pushButton_StartStop->setText("Start");
    ui.spinBox_NumberOfStylusCalibrationPoints->setEnabled(false);

    ui.label_NumberOfPoints->setText(tr("N/A"));
    ui.label_CalibrationError->setText(tr("N/A"));
    ui.label_CurrentPosition->setText(tr("N/A"));
    ui.label_StylusTipTransform->setText(tr("N/A"));

    m_ParentMainWindow->SetStatusBarText(QString(""));
    m_ParentMainWindow->SetStatusBarProgress(-1);
  }
}

//----------------------------------------------------------------------------
vtkPlusPivotCalibrationAlgo* QStylusCalibrationToolbox::GetPivotCalibrationAlgo()
{
  return m_PivotCalibration;
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::StartCalibration()
{
  LOG_TRACE("StylusCalibrationToolbox::Start");

  m_CurrentPointNumber = 0;

  // Clear input points and result point
  m_ParentMainWindow->GetVisualizationController()->SetInputColor(0.0, 0.7, 1.0);
  m_ParentMainWindow->GetVisualizationController()->ClearInputPolyData();
  m_ParentMainWindow->GetVisualizationController()->ClearResultPolyData();

  // Initialize calibration
  m_PivotCalibration->RemoveAllCalibrationPoints();

  // Initialize stylus tool
  vtkPlusDisplayableObject* object = m_ParentMainWindow->GetVisualizationController()->GetObjectById(m_ParentMainWindow->GetStylusModelId());
  if (object == NULL)
  {
    LOG_ERROR("No stylus tip displayable objects could be found!");
  }

  // Set state to in progress
  m_CalibrationStartupDelayStartTime.start();
  SetState(ToolboxState_StartupDelay);
  SetDisplayAccordingToState();

  LOG_INFO("Stylus calibration started");
}

//----------------------------------------------------------------------------
void QStylusCalibrationToolbox::SetFreeHandStartupDelaySec(int freeHandStartupDelaySec)
{
  m_FreeHandStartupDelaySec = freeHandStartupDelaySec;
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::StopCalibration()
{
  LOG_TRACE("StylusCalibrationToolbox::Stop");

  if (m_State == ToolboxState_StartupDelay)
  {
    LOG_TRACE("StylusCalibrationToolbox::Stop before calibration delay timer finished");
    SetState(ToolboxState_Idle);
  }
  else
  {
    // Calibrate async
    PlusStatus result = m_PivotCalibration->DoPivotCalibration(m_ParentMainWindow->GetVisualizationController()->GetTransformRepository());

    if (result == PLUS_SUCCESS)
    {
      LOG_INFO("Stylus calibration successful");

      // Set result point
      double stylustipPosition_Reference[4] = {0, 0, 0, 1};
      m_PivotCalibration->GetPivotPointPosition_Reference(stylustipPosition_Reference);
      vtkPoints* points = m_ParentMainWindow->GetVisualizationController()->GetResultPolyDataPoints();
      points->InsertPoint(0, stylustipPosition_Reference);
      points->Modified();

      // Save result in configuration
      if (m_ParentMainWindow->GetVisualizationController()->GetTransformRepository()->WriteConfiguration(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData()) == PLUS_SUCCESS)
      {
        SetState(ToolboxState_Done);
      }
      else
      {
        LOG_ERROR("Unable to save stylus calibration result in configuration XML tree");
        SetState(ToolboxState_Error);
      }
    }
    else
    {
      LOG_ERROR("Stylus calibration failed!");
      m_CurrentPointNumber = 0;
      SetState(ToolboxState_Error);
    }
  }
  SetDisplayAccordingToState();
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::OnStartStopClicked()
{
  if (m_State == ToolboxState_StartupDelay || m_State == ToolboxState_InProgress)
  {
    StopCalibration();
  }
  else
  {
    StartCalibration();
  }
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::NumberOfStylusCalibrationPointsChanged(int aNumberOfPoints)
{
  LOG_TRACE("StylusCalibrationToolbox::NumberOfStylusCalibrationPointsChanged");
  m_NumberOfPoints = aNumberOfPoints;
}

//-----------------------------------------------------------------------------
void QStylusCalibrationToolbox::OnDataAcquired()
{
  LOG_TRACE("StylusCalibrationToolbox::OnDataAcquired");

  if (m_State == ToolboxState_StartupDelay)
  {
    double elapsedTimeSec = 0.001 * m_CalibrationStartupDelayStartTime.elapsed();
    if (elapsedTimeSec < m_FreeHandStartupDelaySec)
    {
      RefreshContent();
    }
    else
    {
      SetState(ToolboxState_InProgress);
      SetDisplayAccordingToState();
    }
    return;
  }

  // Get stylus position
  vtkSmartPointer<vtkMatrix4x4> stylusToReferenceTransformMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  ToolStatus status(TOOL_INVALID);
  std::string objectMarkerCoordinateFrame = m_PivotCalibration->GetObjectMarkerCoordinateFrame();
  std::string referenceCoordinateFrame = m_PivotCalibration->GetReferenceCoordinateFrame();
  if (m_ParentMainWindow->GetVisualizationController()->GetTransformMatrix(objectMarkerCoordinateFrame.c_str(), referenceCoordinateFrame.c_str(), stylusToReferenceTransformMatrix, &status) != PLUS_SUCCESS)
  {
    LOG_ERROR("No transform found between stylus and reference!");
    return;
  }

  if (status != TOOL_OK)
  {
    return;
  }

  // Assemble position string for toolbox
  m_StylusPositionString = QString("%1 %2 %3")
                           .arg(stylusToReferenceTransformMatrix->GetElement(0, 3), 7, 'f', 1, ' ')
                           .arg(stylusToReferenceTransformMatrix->GetElement(1, 3), 7, 'f', 1, ' ')
                           .arg(stylusToReferenceTransformMatrix->GetElement(2, 3), 7, 'f', 1, ' ');

  // Add point to the input if fulfills the criteria
  vtkSmartPointer<vtkPoints> points = m_ParentMainWindow->GetVisualizationController()->GetInputPolyDataPoints();

  double positionDifferenceLowThresholdMm = 2.0;
  double positionDifferenceHighThresholdMm = 500.0;
  double positionDifferenceMm = -1.0;
  double orientationDifferenceLowThresholdDegrees = 2.0;
  double orientationDifferenceHighThresholdDegrees = 90.0;
  double orientationDifferenceDegrees = -1.0;
  if (m_CurrentPointNumber < 1)
  {
    // Always allow
    positionDifferenceMm = (positionDifferenceLowThresholdMm + positionDifferenceHighThresholdMm) / 2.0;
    orientationDifferenceDegrees = (orientationDifferenceLowThresholdDegrees + orientationDifferenceHighThresholdDegrees) / 2.0;
  }
  else
  {
    // Compute position and orientation difference of current and previous positions
    positionDifferenceMm = igsioMath::GetPositionDifference(stylusToReferenceTransformMatrix, m_PreviousStylusToReferenceTransformMatrix);
    orientationDifferenceDegrees = igsioMath::GetOrientationDifference(stylusToReferenceTransformMatrix, m_PreviousStylusToReferenceTransformMatrix);
  }

  // If current point is close to the previous one, or too far (outlier), we do not insert it
  if (positionDifferenceMm < orientationDifferenceLowThresholdDegrees && orientationDifferenceDegrees < orientationDifferenceLowThresholdDegrees)
  {
    LOG_DEBUG("Acquired position is too close to the previous - it is skipped");
  }
  else
  {
    // Add the point into the calibration dataset
    m_PivotCalibration->InsertNextCalibrationPoint(stylusToReferenceTransformMatrix);

    // Add to polydata for rendering
    points->InsertPoint(m_CurrentPointNumber, stylusToReferenceTransformMatrix->GetElement(0, 3), stylusToReferenceTransformMatrix->GetElement(1, 3), stylusToReferenceTransformMatrix->GetElement(2, 3));
    points->Modified();

    // Set new current point number
    ++m_CurrentPointNumber;

    // Reset the camera once in a while
    if ((m_CurrentPointNumber > 0) && ((m_CurrentPointNumber % 10 == 0) || (m_CurrentPointNumber == 5) || (m_CurrentPointNumber >= m_NumberOfPoints)))
    {
      m_ParentMainWindow->GetVisualizationController()->GetCanvasRenderer()->ResetCamera();
    }

    // If enough points have been acquired, stop
    if (m_CurrentPointNumber >= m_NumberOfPoints)
    {
      StopCalibration();
    }
    else
    {
      m_PreviousStylusToReferenceTransformMatrix->DeepCopy(stylusToReferenceTransformMatrix);
    }
  }
}