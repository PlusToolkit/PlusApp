/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "vtkImageData.h"
#include "vtkObjectFactory.h"
#include "vtkPlusBuffer.h"
#include "vtkTrackedFrameList.h"
#include "vtkPlusChannel.h"
#include "vtkPlusDataSource.h"
#include "vtkUsSimulatorVideoSource.h"
#include "vtkTransformRepository.h"

vtkCxxRevisionMacro(vtkUsSimulatorVideoSource, "$Revision: 1.0$");
vtkStandardNewMacro(vtkUsSimulatorVideoSource);

//----------------------------------------------------------------------------
vtkUsSimulatorVideoSource::vtkUsSimulatorVideoSource()
: UsSimulator(NULL)
, Tracker(NULL)
, LastProcessedTrackingDataTimestamp(0)
, GracePeriodLogLevel(vtkPlusLogger::LOG_LEVEL_DEBUG)
{
  // Create and set up US simulator
  vtkSmartPointer<vtkUsSimulatorAlgo> usSimulator = vtkSmartPointer<vtkUsSimulatorAlgo>::New();
  this->SetUsSimulator(usSimulator);

  // Create transform repository
  vtkSmartPointer<vtkTransformRepository> transformRepository = vtkSmartPointer<vtkTransformRepository>::New();
  this->GetUsSimulator()->SetTransformRepository(transformRepository);

  this->RequireImageOrientationInConfiguration = true;
  this->RequireFrameBufferSizeInDeviceSetConfiguration = true;
  this->RequireAcquisitionRateInDeviceSetConfiguration = false;
  this->RequireAveragedItemsForFilteringInDeviceSetConfiguration = false;
  this->RequireLocalTimeOffsetSecInDeviceSetConfiguration = false;
  this->RequireUsImageOrientationInDeviceSetConfiguration = true;
  this->RequireRfElementInDeviceSetConfiguration = false;

  // No callback function provided by the device, so the data capture thread will be used to poll the hardware and add new items to the buffer
  this->StartThreadForInternalUpdates = true;
}

//----------------------------------------------------------------------------
vtkUsSimulatorVideoSource::~vtkUsSimulatorVideoSource()
{ 
  if (!this->Connected)
  {
    this->Disconnect();
  }

  this->SetTracker(NULL);
  this->SetUsSimulator(NULL);
}

//----------------------------------------------------------------------------
void vtkUsSimulatorVideoSource::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}

//----------------------------------------------------------------------------
PlusStatus vtkUsSimulatorVideoSource::InternalUpdate()
{
  //LOG_TRACE("vtkUsSimulatorVideoSource::InternalUpdate");

  if( this->InputChannels.size() != 1 )
  {
    LOG_ERROR("vtkUsSimulatorVideoSource device requires exactly 1 input stream (that contains the tracking data). Check configuration.");
    return PLUS_FAIL;
  }

  if( this->HasGracePeriodExpired() )
  {
    this->GracePeriodLogLevel = vtkPlusLogger::LOG_LEVEL_WARNING;
  }

  // Get image to tracker transform from the tracker (only request 1 frame, the latest)
  vtkSmartPointer<vtkTrackedFrameList> trackingFrames = vtkSmartPointer<vtkTrackedFrameList>::New();  
  if (!this->InputChannels[0]->GetTrackingDataAvailable())
  {
    LOG_DEBUG("Simulated US image is not generated, as no tracking data is available yet. Device ID: " << this->GetDeviceId() ); 
    return PLUS_SUCCESS;
  }
  double oldestTrackingTimestamp(0);
  if (this->InputChannels[0]->GetOldestTimestamp(oldestTrackingTimestamp) == PLUS_SUCCESS)
  {
    if (this->LastProcessedTrackingDataTimestamp < oldestTrackingTimestamp)
    {
      LOG_INFO("Simulated US image generation started. No tracking data was available between " << this->LastProcessedTrackingDataTimestamp << "-" << oldestTrackingTimestamp <<
        "sec, therefore no simulated images were generated during this time period.");
      this->LastProcessedTrackingDataTimestamp = oldestTrackingTimestamp;
    }
  }
  if ( this->InputChannels[0]->GetTrackedFrameList(this->LastProcessedTrackingDataTimestamp, trackingFrames, 1) != PLUS_SUCCESS )
  {
    LOG_ERROR("Error while getting tracked frame list from data collector during capturing. Last recorded timestamp: " << std::fixed << this->LastProcessedTrackingDataTimestamp << ". Device ID: " << this->GetDeviceId() ); 
    this->LastProcessedTrackingDataTimestamp = vtkAccurateTimer::GetSystemTime(); // forget about the past, try to add frames that are acquired from now on
    return PLUS_FAIL;
  }
  if (trackingFrames->GetNumberOfTrackedFrames() < 1)
  {
    LOG_DYNAMIC("Simulated US image is not generated, as no tracking data is available. Device ID: " << this->GetDeviceId(), this->GracePeriodLogLevel ); 
    return PLUS_FAIL;
  }
  TrackedFrame* trackedFrame = trackingFrames->GetTrackedFrame(0);
  if (trackedFrame == NULL)
  {
    LOG_ERROR("Error while getting tracked frame from data collector during capturing. Last recorded timestamp: " << std::fixed << this->LastProcessedTrackingDataTimestamp << ". Device ID: " << this->GetDeviceId() ); 
    return PLUS_FAIL;
  }

  // Get latest tracker timestamp
  double latestTrackerTimestamp = trackedFrame->GetTimestamp();
  
  double latestFrameAlreadyAddedTimestamp=0;
  this->OutputChannels[0]->GetMostRecentTimestamp(latestFrameAlreadyAddedTimestamp);
  if (latestFrameAlreadyAddedTimestamp>=latestTrackerTimestamp)
  {
    // simulated frame has been already generated for this timestamp
    return PLUS_SUCCESS;
  }

  // The sampling rate is constant, so to have a constant frame rate we have to increase the FrameNumber by a constant.
  // For simplicity, we increase it always by 1.
  this->FrameNumber++;
  
  if ( this->UsSimulator->GetTransformRepository()->SetTransforms(*trackedFrame) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to set repository transforms from tracked frame!"); 
    return PLUS_FAIL;
  }

  // Get the simulated US image  
  this->UsSimulator->Modified(); // Signal that the transforms have changed so we need to recompute
  this->UsSimulator->Update();

  vtkPlusDataSource* aSource(NULL);
  if( this->OutputChannels[0]->GetVideoSource(aSource) != PLUS_SUCCESS )
  {
    LOG_ERROR("Unable to retrieve the video source in the USSimulator device.");
    return PLUS_FAIL;
  }

  PlusStatus status = aSource->GetBuffer()->AddItem(
    this->UsSimulator->GetOutput(), aSource->GetPortImageOrientation(), US_IMG_BRIGHTNESS, this->FrameNumber, latestTrackerTimestamp, latestTrackerTimestamp);

  this->Modified();
  return status;
}

//----------------------------------------------------------------------------
PlusStatus vtkUsSimulatorVideoSource::InternalConnect()
{
  LOG_TRACE("vtkUsSimulatorVideoSource::InternalConnect"); 

  vtkPlusDataSource* aSource(NULL);
  if( this->OutputChannels[0]->GetVideoSource(aSource) != PLUS_SUCCESS )
  {
    LOG_ERROR("Unable to retrieve the video source in the USSimulator device.");
    return PLUS_FAIL;
  }

  // Set to default MF internal image orientation
  aSource->SetPortImageOrientation(US_IMG_ORIENT_MF);
  aSource->GetBuffer()->Clear();
  int frameSize[2]={0,0};
  if (this->UsSimulator->GetFrameSize(frameSize)!=PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to initialize buffer, frame size is unknown");
    return PLUS_FAIL;
  }
  aSource->GetBuffer()->SetFrameSize(frameSize);

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkUsSimulatorVideoSource::InternalDisconnect()
{
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkUsSimulatorVideoSource::ReadConfiguration(vtkXMLDataElement* config)
{
  LOG_TRACE("vtkUsSimulatorVideoSource::ReadConfiguration"); 
  if ( config == NULL )
  {
    LOG_ERROR("Unable to configure Saved Data video source! (XML data element is NULL)"); 
    return PLUS_FAIL; 
  }

  // Read superclass configuration
  Superclass::ReadConfiguration(config); 

  vtkXMLDataElement* deviceConfig = this->FindThisDeviceElement(config);
  if (deviceConfig == NULL) 
  {
    LOG_ERROR("Unable to find US simulator device element in configuration XML structure!");
    return PLUS_FAIL;
  }

  // Read US simulator configuration
  if ( !this->UsSimulator
    || this->UsSimulator->ReadConfiguration(deviceConfig) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to read US simulator configuration!");
    return PLUS_FAIL;
  }

  // Read transform repository configuration
  if ( !this->UsSimulator->GetTransformRepository()
    || this->UsSimulator->GetTransformRepository()->ReadConfiguration(config) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to read transform repository configuration!"); 
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkUsSimulatorVideoSource::NotifyConfigured()
{
  if( this->OutputChannels.size() > 1 )
  {
    LOG_WARNING("vtkUsSimulatorVideoSource is expecting one output channel and there are " << this->OutputChannels.size() << " channels. First output channel will be used.");
    return PLUS_FAIL;
  }

  if( this->OutputChannels.size() == 0 )
  {
    LOG_ERROR("No output channels defined for vtkUsSimulatorVideoSource. Cannot proceed." );
    this->SetCorrectlyConfigured(false);
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}