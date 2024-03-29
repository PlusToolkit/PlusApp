/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "QCapturingToolbox.h"
#include "QVolumeReconstructionToolbox.h"
#include "fCalMainWindow.h"
#include "vtkPlusVisualizationController.h"

// PlusLib includes
#include <vtkPlusSequenceIO.h>
#include <vtkIGSIOTrackedFrameList.h>
#include <vtkPlusVolumeReconstructor.h>

// VTK includes
#include <vtkImageData.h>
#include <vtkMarchingContourFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkXMLUtilities.h>

// Qt includes
#include <QFileDialog>

//-----------------------------------------------------------------------------
QVolumeReconstructionToolbox::QVolumeReconstructionToolbox(fCalMainWindow* aParentMainWindow, Qt::WindowFlags aFlags)
  : QAbstractToolbox(aParentMainWindow)
  , QWidget(aParentMainWindow, aFlags)
  , m_VolumeReconstructor(NULL)
  , m_ReconstructedVolume(NULL)
  , m_VolumeReconstructionConfigFileLoaded(false)
  , m_VolumeReconstructionComplete(false)
  , m_ContouringThreshold(64.0)
{
  ui.setupUi(this);

  m_VolumeReconstructor = vtkPlusVolumeReconstructor::New();
  m_ReconstructedVolume = vtkImageData::New();

  // Connect events
  connect(ui.pushButton_OpenVolumeReconstructionConfig, SIGNAL(clicked()), this, SLOT(OpenVolumeReconstructionConfig()));
  connect(ui.pushButton_OpenInputImage, SIGNAL(clicked()), this, SLOT(OpenInputImage()));
  connect(ui.comboBox_InputImage, SIGNAL(currentIndexChanged(int)), this, SLOT(InputImageChanged(int)));
  connect(ui.horizontalSlider_ContouringThreshold, SIGNAL(valueChanged(int)), this, SLOT(RecomputeContourFromReconstructedVolume(int)));
  connect(ui.pushButton_Reconstruct, SIGNAL(clicked()), this, SLOT(Reconstruct()));
  connect(ui.pushButton_Save, SIGNAL(clicked()), this, SLOT(Save()));

  m_LastSaveLocation = vtkPlusConfig::GetInstance()->GetImageDirectory().c_str();
}

//-----------------------------------------------------------------------------
QVolumeReconstructionToolbox::~QVolumeReconstructionToolbox()
{
  if (m_VolumeReconstructor != NULL)
  {
    m_VolumeReconstructor->Delete();
    m_VolumeReconstructor = NULL;
  }

  if (m_ReconstructedVolume != NULL)
  {
    m_ReconstructedVolume->Delete();
    m_ReconstructedVolume = NULL;
  }
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::OnActivated()
{
  LOG_TRACE("VolumeReconstructionToolbox::OnActivated");

  // Try to load volume reconstruction configuration from the device set configuration
  if ((vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData() != NULL)
      && (m_VolumeReconstructor->ReadConfiguration(vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationData()) == PLUS_SUCCESS))
  {
    m_VolumeReconstructionConfigFileLoaded = true;
  }

  // Clear results polydata
  if (m_State != ToolboxState_Done)
  {
    m_ParentMainWindow->GetVisualizationController()->ClearResultPolyData();
  }

  // Check for new images
  PopulateImageComboBox();

  // Set initialized if it was uninitialized
  if (m_State == ToolboxState_Uninitialized || m_State == ToolboxState_Error)
  {
    SetState(ToolboxState_Idle);
  }
  else
  {
    SetDisplayAccordingToState();
  }
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::RefreshContent()
{
  //LOG_TRACE("VolumeReconstructionToolbox::RefreshContent");

  ui.label_ContouringThreshold->setText(QString::number(m_ContouringThreshold));

  if (m_State == ToolboxState_InProgress)
  {
    // Needed for forced refreshing the UI (without this, no progress is shown)
    QApplication::processEvents();
  }
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::SetDisplayAccordingToState()
{
  LOG_TRACE("VolumeReconstructionToolbox::SetDisplayAccordingToState");

  // If the force show devices isn't enabled, set it to 3D and hide all the devices
  // Later, we will re-enable only those that we wish shown for this toolbox
  if (!m_ParentMainWindow->IsForceShowDevicesEnabled())
  {
    m_ParentMainWindow->GetVisualizationController()->SetVisualizationMode(vtkPlusVisualizationController::DISPLAY_MODE_3D);
    m_ParentMainWindow->GetVisualizationController()->HideAll();
  }

  // Set it to 3D mode
  m_ParentMainWindow->GetVisualizationController()->SetVisualizationMode(vtkPlusVisualizationController::DISPLAY_MODE_3D);

  // Enable or disable the image manipulation menu
  m_ParentMainWindow->SetImageManipulationMenuEnabled(m_ParentMainWindow->GetVisualizationController()->Is2DMode());

  // Update state message according to available transforms
  if (!m_ParentMainWindow->GetImageCoordinateFrame().empty() && !m_ParentMainWindow->GetProbeCoordinateFrame().empty())
  {
    std::string imageToProbeTransformNameStr;
    igsioTransformName imageToProbeTransformName(
      m_ParentMainWindow->GetImageCoordinateFrame(), m_ParentMainWindow->GetProbeCoordinateFrame());
    imageToProbeTransformName.GetTransformName(imageToProbeTransformNameStr);

    if (m_ParentMainWindow->GetVisualizationController()->IsExistingTransform(
          m_ParentMainWindow->GetImageCoordinateFrame().c_str(), m_ParentMainWindow->GetProbeCoordinateFrame().c_str(), false) == PLUS_SUCCESS)
    {
      std::string date, errorStr;
      double error;
      if (m_ParentMainWindow->GetVisualizationController()->GetTransformRepository()->GetTransformDate(imageToProbeTransformName, date) != PLUS_SUCCESS)
      {
        date = "N/A";
      }
      if (m_ParentMainWindow->GetVisualizationController()->GetTransformRepository()->GetTransformError(imageToProbeTransformName, error) == PLUS_SUCCESS)
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
      ui.label_State->setText(QString("%1 transform present, ready for volume reconstruction. \nDate: %2, Error: %3").arg(imageToProbeTransformNameStr.c_str()).arg(date.c_str()).arg(errorStr.c_str()));
    }
    else
    {
      QPalette palette;
      palette.setColor(ui.label_State->foregroundRole(), QColor::fromRgb(255, 128, 0));
      ui.label_State->setPalette(palette);
      ui.label_State->setText(QString("%1 transform is absent, spatial calibration needs to be performed or imported.").arg(imageToProbeTransformNameStr.c_str()));
      LOG_INFO(imageToProbeTransformNameStr << " transform is absent, spatial calibration needs to be performed or imported.");
      m_State = ToolboxState_Uninitialized;
    }
  }
  else
  {
    QPalette palette;
    palette.setColor(ui.label_State->foregroundRole(), QColor::fromRgb(255, 128, 0));
    ui.label_State->setPalette(palette);
    ui.label_State->setText(QString("fCal configuration element does not contain both ImageCoordinateFrame and ProbeCoordinateFrame attributes!"));
    LOG_INFO("fCal configuration element does not contain both ImageCoordinateFrame and ProbeCoordinateFrame attributes");
    m_State = ToolboxState_Uninitialized;
  }

  // Set widget states according to state
  if (m_State == ToolboxState_Uninitialized)
  {
    ui.label_Instructions->setText("N/A");
    ui.horizontalSlider_ContouringThreshold->setEnabled(false);

    ui.pushButton_Reconstruct->setEnabled(false);
    ui.pushButton_Save->setEnabled(false);

  }
  else if (m_State == ToolboxState_Idle)
  {
    ui.label_Instructions->setText(tr("N/A"));
    ui.horizontalSlider_ContouringThreshold->setEnabled(m_VolumeReconstructionComplete);
    ui.pushButton_Save->setEnabled(m_VolumeReconstructionComplete);

    if (! m_VolumeReconstructionConfigFileLoaded)
    {
      ui.label_Instructions->setText(tr("Volume reconstruction config XML has to be loaded"));
      ui.pushButton_Reconstruct->setEnabled(false);
    }
    else if ((ui.comboBox_InputImage->currentIndex() == -1) || (ui.comboBox_InputImage->count() == 0) || (ui.comboBox_InputImage->isEnabled() == false))
    {
      ui.label_Instructions->setText(tr("Input image has to be selected"));
      ui.pushButton_Reconstruct->setEnabled(false);
    }
    else
    {
      ui.label_Instructions->setText(tr("Press Reconstruct button start reconstruction"));
      ui.pushButton_Reconstruct->setEnabled(true);
      ui.comboBox_InputImage->setToolTip(ui.comboBox_InputImage->currentText());
    }

    m_ParentMainWindow->GetVisualizationController()->EnableVolumeActor(m_VolumeReconstructionComplete);
  }
  else if (m_State == ToolboxState_InProgress)
  {
    ui.label_Instructions->setText("");
    ui.horizontalSlider_ContouringThreshold->setEnabled(false);

    ui.pushButton_Reconstruct->setEnabled(false);
    ui.pushButton_Save->setEnabled(false);

  }
  else if (m_State == ToolboxState_Done)
  {
    ui.label_Instructions->setText("Reconstruction done");
    ui.horizontalSlider_ContouringThreshold->setEnabled(true);

    ui.pushButton_Reconstruct->setEnabled(false);
    ui.pushButton_Save->setEnabled(true);

    m_ParentMainWindow->SetStatusBarText(QString(" Reconstruction done"));
    m_ParentMainWindow->SetStatusBarProgress(-1);

    m_ParentMainWindow->GetVisualizationController()->EnableVolumeActor(true);
    m_ParentMainWindow->GetVisualizationController()->GetCanvasRenderer()->Modified();
    m_ParentMainWindow->GetVisualizationController()->GetCanvasRenderer()->ResetCamera();
  }
  else if (m_State == ToolboxState_Error)
  {
    ui.label_Instructions->setText("Error occurred!");
    ui.horizontalSlider_ContouringThreshold->setEnabled(false);

    ui.pushButton_Reconstruct->setEnabled(false);
    ui.pushButton_Save->setEnabled(false);
  }
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::OpenVolumeReconstructionConfig()
{
  LOG_TRACE("VolumeReconstructionToolbox::OpenVolumeReconstructionConfig");

  // File open dialog for selecting phantom definition xml
  QString filter = QString(tr("XML files ( *.xml );;"));
  QString fileName = QFileDialog::getOpenFileName(NULL, QString(tr("Open volume reconstruction configuration XML")),
                     vtkPlusConfig::GetInstance()->GetDeviceSetConfigurationDirectory().c_str(), filter);
  if (fileName.isNull())
  {
    return;
  }

  // Parse XML file
  vtkSmartPointer<vtkXMLDataElement> rootElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(fileName.toLatin1().constData()));
  if (rootElement == NULL)
  {
    LOG_ERROR("Unable to read the configuration file: " << fileName.toLatin1().constData());
    return;
  }

  // Load volume reconstruction configuration xml
  if (m_VolumeReconstructor->ReadConfiguration(rootElement) != PLUS_SUCCESS)
  {
    m_VolumeReconstructionConfigFileLoaded = false;

    LOG_ERROR("Failed to import volume reconstruction settings from " << fileName.toLatin1().constData());
    return;
  }

  m_VolumeReconstructionConfigFileLoaded = true;

  SetState(ToolboxState_Idle);

  LOG_INFO("Volume reconstruction configuration imported in volume reconstruction toolbox from file '" << fileName.toLatin1().constData() << "'");
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::OpenInputImage()
{
  LOG_TRACE("VolumeReconstructionToolbox::OpenInputImage");

  // File open dialog for selecting phantom definition xml
  QString filter = QString(tr("MHA files ( *.mha );;"));
  QString fileName = QFileDialog::getOpenFileName(NULL, QString(tr("Open input sequence metafile image")), "", filter);

  if (fileName.isNull())
  {
    return;
  }

  m_ImageFileNames.append(fileName);

  PopulateImageComboBox();

  ui.comboBox_InputImage->setCurrentIndex(ui.comboBox_InputImage->count() - 1);

  SetState(ToolboxState_Idle);

  LOG_INFO("Input image '" << fileName.toLatin1().constData() << "' opened");
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::Reconstruct()
{
  LOG_TRACE("VolumeReconstructionToolbox::Reconstruct");

  QApplication::setOverrideCursor(QCursor(Qt::BusyCursor));

  if (ReconstructVolumeFromInputImage() != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to reconstruct volume!");
    SetState(ToolboxState_Error);
  }

  QApplication::restoreOverrideCursor();

  LOG_INFO("Volume reconstruction performed successfully");
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::Save()
{
  LOG_TRACE("VolumeReconstructionToolbox::Save");

  QString filter = QString(tr("Sequence metafiles ( *.mha );;"));
  QString fileName = QFileDialog::getSaveFileName(NULL, tr("Save reconstructed volume"), m_LastSaveLocation, filter);

  if (! fileName.isNull())
  {
    QApplication::setOverrideCursor(QCursor(Qt::BusyCursor));

    if (SaveVolumeToFile(fileName) != PLUS_SUCCESS)
    {
      LOG_ERROR("Unable to save volume to file!");
    }

    QApplication::restoreOverrideCursor();

    LOG_INFO("Reconstructed volume saved into file '" << fileName.toLatin1().constData() << "'");
  }
}

//-----------------------------------------------------------------------------
PlusStatus QVolumeReconstructionToolbox::ReconstructVolumeFromInputImage()
{
  LOG_TRACE("VolumeReconstructionToolbox::ReconstructVolumeFromInputImage");

  SetState(ToolboxState_InProgress);

  // Read image
  m_ParentMainWindow->SetStatusBarText(QString(" Reading image sequence ..."));
  m_ParentMainWindow->SetStatusBarProgress(0);
  RefreshContent();

  vtkSmartPointer<vtkIGSIOTrackedFrameList> trackedFrameList = NULL;

  if (ui.comboBox_InputImage->currentText().left(1) == "<" && ui.comboBox_InputImage->currentText().right(1) == ">")       // If unsaved image is selected
  {
    QCapturingToolbox* capturingToolbox = dynamic_cast<QCapturingToolbox*>(m_ParentMainWindow->GetToolbox(ToolboxType_Capturing));
    if ((capturingToolbox == NULL) || ((trackedFrameList = capturingToolbox->GetRecordedFrames()) == NULL))
    {
      LOG_ERROR("Unable to get recorded frame list from Capturing toolbox!");
      return PLUS_FAIL;
    }
  }
  else
  {
    int imageFileNameIndex = -1;
    if (ui.comboBox_InputImage->itemText(0).left(1) == "<" && ui.comboBox_InputImage->itemText(0).right(1) == ">")           // If unsaved image exists
    {
      imageFileNameIndex = ui.comboBox_InputImage->currentIndex() - 1;
    }
    else
    {
      imageFileNameIndex = ui.comboBox_InputImage->currentIndex();
    }
    trackedFrameList = vtkSmartPointer<vtkIGSIOTrackedFrameList>::New();
    if (vtkPlusSequenceIO::Read(m_ImageFileNames.at(imageFileNameIndex).toLatin1().constData(), trackedFrameList) != PLUS_SUCCESS)
    {
      LOG_ERROR("Unable to load input image file!");
      return PLUS_FAIL;
    }
  }


  m_ParentMainWindow->SetStatusBarText(QString(" Reconstructing volume ..."));
  m_ParentMainWindow->SetStatusBarProgress(0);
  RefreshContent();

  m_VolumeReconstructor->SetReferenceCoordinateFrame(m_ParentMainWindow->GetReferenceCoordinateFrame().c_str());
  m_VolumeReconstructor->SetImageCoordinateFrame(m_ParentMainWindow->GetImageCoordinateFrame().c_str());

  std::string errorDetail;
  if (m_VolumeReconstructor->SetOutputExtentFromFrameList(trackedFrameList,
      m_ParentMainWindow->GetVisualizationController()->GetTransformRepository(), errorDetail) == PLUS_FAIL)
  {
    return PLUS_FAIL;
  }

  const int numberOfFrames = trackedFrameList->GetNumberOfTrackedFrames();
  for (int frameIndex = 0; frameIndex < numberOfFrames; frameIndex += m_VolumeReconstructor->GetSkipInterval())
  {
    // Set progress
    m_ParentMainWindow->SetStatusBarProgress((int)((100.0 * frameIndex) / numberOfFrames + 0.49));
    RefreshContent();

    igsioTrackedFrame* frame = trackedFrameList->GetTrackedFrame(frameIndex);

    if (m_ParentMainWindow->GetVisualizationController()->GetTransformRepository()->SetTransforms(*frame) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to update transform repository with frame #" << frameIndex);
      continue;
    }

    // Add this tracked frame to the reconstructor
    bool insertedIntoVolume = false;
    if (m_VolumeReconstructor->AddTrackedFrame(frame, m_ParentMainWindow->GetVisualizationController()->GetTransformRepository(), frameIndex == 0, frameIndex == numberOfFrames-1, &insertedIntoVolume) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to add tracked frame to volume with frame #" << frameIndex);
      continue;
    }
  }

  m_ParentMainWindow->SetStatusBarProgress(0);
  RefreshContent();

  trackedFrameList->Clear();

  m_ParentMainWindow->SetStatusBarProgress(0);
  m_ParentMainWindow->SetStatusBarText(QString(" Filling holes in output volume..."));
  RefreshContent();

  m_VolumeReconstructor->ExtractGrayLevels(m_ReconstructedVolume);

  // Display result
  DisplayReconstructedVolume();

  m_ParentMainWindow->SetStatusBarProgress(100);

  m_VolumeReconstructionComplete = true;

  SetState(ToolboxState_Done);

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::DisplayReconstructedVolume()
{
  LOG_TRACE("VolumeReconstructionToolbox::DisplayReconstructedVolume");

  m_ParentMainWindow->SetStatusBarText(QString(" Generating contour for displaying..."));
  RefreshContent();

  vtkSmartPointer<vtkMarchingContourFilter> contourFilter = vtkSmartPointer<vtkMarchingContourFilter>::New();
  contourFilter->SetInputData(m_ReconstructedVolume);
  contourFilter->SetValue(0, m_ContouringThreshold);

  vtkSmartPointer<vtkPolyDataMapper> contourMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  contourMapper->SetInputConnection(contourFilter->GetOutputPort());

  m_ParentMainWindow->GetVisualizationController()->SetVolumeMapper(contourMapper);
  m_ParentMainWindow->GetVisualizationController()->SetVolumeColor(0.0, 0.0, 1.0);
}

//-----------------------------------------------------------------------------
PlusStatus QVolumeReconstructionToolbox::SaveVolumeToFile(QString aOutput)
{
  LOG_TRACE("VolumeReconstructionToolbox::SaveVolumeToFile(" << aOutput.toLatin1().constData() << ")");

  if (aOutput.right(3).toLower() == QString("mha"))
  {
    if (m_VolumeReconstructor->SaveReconstructedVolumeToMetafile(aOutput.toStdString()) != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to save reconstructed volume in sequence metafile!");
      return PLUS_FAIL;
    }
  }
  else
  {
    LOG_ERROR("Invalid file extension (.mha expected)!");
    return PLUS_FAIL;
  }

  m_LastSaveLocation = aOutput.mid(0, aOutput.lastIndexOf('/'));

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::AddImageFileName(QString aImageFileName)
{
  LOG_TRACE("VolumeReconstructionToolbox::AddImageFileName(" << aImageFileName.toLatin1().constData() << ")");

  m_ImageFileNames.append(aImageFileName);
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::PopulateImageComboBox()
{
  LOG_TRACE("VolumeReconstructionToolbox::PopulateImageComboBox");

  // Clear images combobox
  if (ui.comboBox_InputImage->count() > 0)
  {
    for (int i = ui.comboBox_InputImage->count() - 1; i >= 0; --i)
    {
      ui.comboBox_InputImage->removeItem(i);
    }
  }

  // Get recorded tracked frame list from Capturing toolbox
  vtkIGSIOTrackedFrameList* recordedFrames = NULL;
  QCapturingToolbox* capturingToolbox = dynamic_cast<QCapturingToolbox*>(m_ParentMainWindow->GetToolbox(ToolboxType_Capturing));
  if ((capturingToolbox == NULL) || ((recordedFrames = capturingToolbox->GetRecordedFrames()) == NULL))
  {
    LOG_ERROR("Capturing toolbox not found!");
    return;
  }

  if (recordedFrames->GetNumberOfTrackedFrames() > 0)
  {
    ui.comboBox_InputImage->addItem("<unsaved image from Capturing>");
    ui.comboBox_InputImage->setCurrentIndex(0);
  }

  // Add images from the stored list
  QStringListIterator imagesIterator(m_ImageFileNames);
  while (imagesIterator.hasNext())
  {
    QString imageFileName(imagesIterator.next());
    int lastIndexOfSlash = imageFileName.lastIndexOf('/');
    int lastIndexOfBackslash = imageFileName.lastIndexOf('\\');
    ui.comboBox_InputImage->addItem(imageFileName);
  }

  if (ui.comboBox_InputImage->count() > 0)
  {
    ui.comboBox_InputImage->setEnabled(true);
  }
  else // Disable the combobox and indicate that it is empty
  {
    ui.comboBox_InputImage->addItem(tr("Open or capture image first"));
    ui.comboBox_InputImage->setEnabled(false);
  }
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::InputImageChanged(int aItemIndex)
{
  LOG_TRACE("VolumeReconstructionToolbox::InputImageChanged(" << aItemIndex << ")");

  SetState(ToolboxState_Idle);
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::RecomputeContourFromReconstructedVolume(int aValue)
{
  LOG_TRACE("VolumeReconstructionToolbox::UpdateContourThresholdLabel(" << aValue << ")");

  m_ContouringThreshold = ui.horizontalSlider_ContouringThreshold->value();

  LOG_INFO("Recomputing controur from reconstructed volume using threshold " << m_ContouringThreshold);

  DisplayReconstructedVolume();
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::Reset()
{
  QAbstractToolbox::Reset();

  m_VolumeReconstructionComplete = false;

  if (m_VolumeReconstructor != NULL)
  {
    m_VolumeReconstructor->Delete();
    m_VolumeReconstructor = NULL;
  }
  if (m_ReconstructedVolume != NULL)
  {
    m_ReconstructedVolume->Delete();
    m_ReconstructedVolume = NULL;
  }
  m_VolumeReconstructor = vtkPlusVolumeReconstructor::New();
  m_ReconstructedVolume = vtkImageData::New();
}

//-----------------------------------------------------------------------------
void QVolumeReconstructionToolbox::OnDeactivated()
{

}