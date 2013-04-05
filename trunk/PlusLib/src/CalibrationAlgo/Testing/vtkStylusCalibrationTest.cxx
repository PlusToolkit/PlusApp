/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/ 

/*!
  \file vtkStylusCalibrationTest.cxx 
  \brief This test runs a stylus (pivot) calibration on a recorded data set 
  and compares the results to a baseline
*/ 

#include "PlusConfigure.h"
#include "PlusMath.h"
#include "TrackedFrame.h"
#include "vtkDataCollector.h"
#include "vtkMatrix4x4.h"
#include "vtkPivotCalibrationAlgo.h"
#include "vtkPlusChannel.h"
#include "vtkPlusConfig.h"
#include "vtkSmartPointer.h"
#include "vtkTrackedFrameList.h"
#include "vtkTransform.h"
#include "vtkTransformRepository.h"
#include "vtkXMLDataElement.h"
#include "vtkXMLUtilities.h"
#include "vtksys/CommandLineArguments.hxx" 
#include "vtksys/SystemTools.hxx"
#include <iostream>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////
const double TRANSLATION_ERROR_THRESHOLD = 0.5; // error threshold is 0.5mm
const double ROTATION_ERROR_THRESHOLD = 0.5; // error threshold is 0.5deg

int CompareCalibrationResultsWithBaseline(const char* baselineFileName, const char* currentResultFileName, const char* stylusCoordinateFrame, const char* stylusTipCoordinateFrame);

int main (int argc, char* argv[])
{ 
  std::string inputConfigFileName;
  std::string inputBaselineFileName;

  int numberOfPointsToAcquire=100;
  int verboseLevel=vtkPlusLogger::LOG_LEVEL_UNDEFINED;

  vtksys::CommandLineArguments cmdargs;
  cmdargs.Initialize(argc, argv);

  cmdargs.AddArgument("--config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputConfigFileName, "Configuration file name");
  cmdargs.AddArgument("--baseline-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &inputBaselineFileName, "Name of file storing baseline calibration results");
  cmdargs.AddArgument("--number-of-points-to-acquire", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &numberOfPointsToAcquire, "Number of acquired points during the pivot calibration (default: 100)");
  cmdargs.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug, 5=trace)");  

  if ( !cmdargs.Parse() )
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << cmdargs.GetHelp() << std::endl;
    exit(EXIT_FAILURE);
  }

  vtkPlusLogger::Instance()->SetLogLevel(verboseLevel);

  std::string programPath("./"), errorMsg; 
  if ( !vtksys::SystemTools::FindProgramPath(argv[0], programPath, errorMsg) )
  {
    LOG_ERROR(errorMsg); 
  }
  programPath = vtksys::SystemTools::GetParentDirectory(programPath.c_str()); 

  LOG_INFO("Initialize"); 

  // Read configuration
  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(inputConfigFileName.c_str()));
  if (configRootElement == NULL)
  {  
    LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str()); 
    exit(EXIT_FAILURE);
  }

  vtkPlusConfig::GetInstance()->SetDeviceSetConfigurationData(configRootElement); 

  // Initialize data collection
  vtkSmartPointer<vtkDataCollector> dataCollector = vtkSmartPointer<vtkDataCollector>::New(); 
  if (dataCollector->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to parse configuration from file " << inputConfigFileName.c_str()); 
    exit(EXIT_FAILURE);
  }

  if (dataCollector->Connect() != PLUS_SUCCESS) {
    LOG_ERROR("Unable to initialize data collection!");
    exit(EXIT_FAILURE);
  }
  if (dataCollector->Start() != PLUS_SUCCESS) {
    LOG_ERROR("Unable to start data collection!");
    exit(EXIT_FAILURE);
  }
  vtkPlusChannel* aChannel(NULL);
  vtkPlusDevice* aDevice(NULL);
  if( dataCollector->GetDevice(aDevice, std::string("TrackerDevice")) != PLUS_SUCCESS )
  {
    LOG_ERROR("Unable to locate device by ID: \'TrackerDevice\'");
    exit(EXIT_FAILURE);
  }
  if( aDevice->GetOutputChannelByName(aChannel, "TrackerStream") != PLUS_SUCCESS )
  {
    LOG_ERROR("Unable to locate channel by ID: \'TrackerStream\'");
    exit(EXIT_FAILURE);
  }
  if ( aChannel->GetTrackingDataAvailable() == false ) {
    LOG_ERROR("Channel \'" << aChannel->GetChannelId() << "\' is not tracking!");
    exit(EXIT_FAILURE);
  }

  // Initialize stylus calibration
  vtkSmartPointer<vtkPivotCalibrationAlgo> pivotCalibration = vtkSmartPointer<vtkPivotCalibrationAlgo>::New();
  if (pivotCalibration == NULL)
  {
    LOG_ERROR("Unable to instantiate pivot calibration algorithm class!");
    exit(EXIT_FAILURE);
  }

  if (pivotCalibration->ReadConfiguration(configRootElement) != PLUS_SUCCESS)
  {
    LOG_ERROR("Unable to read pivot calibration configuration!");
    exit(EXIT_FAILURE);
  }

  // Create and initialize transform repository
  TrackedFrame trackedFrame;
  aChannel->GetTrackedFrame(&trackedFrame);

  vtkSmartPointer<vtkTransformRepository> transformRepository = vtkSmartPointer<vtkTransformRepository>::New();
  transformRepository->SetTransforms(trackedFrame);

  // Check stylus tool
  PlusTransformName stylusToReferenceTransformName(pivotCalibration->GetObjectMarkerCoordinateFrame(), pivotCalibration->GetReferenceCoordinateFrame());

  // Acquire positions for pivot calibration
  for (int i=0; i < numberOfPointsToAcquire; ++i)
  {
    vtksys::SystemTools::Delay(50);
    vtkPlusLogger::PrintProgressbar((100.0 * i) / numberOfPointsToAcquire); 

    vtkSmartPointer<vtkMatrix4x4> stylusToReferenceMatrix = vtkSmartPointer<vtkMatrix4x4>::New();

    
    TrackedFrame trackedFrame; 
    if ( aChannel->GetTrackedFrame(&trackedFrame) != PLUS_SUCCESS )
    {
      LOG_ERROR("Failed to get tracked frame!"); 
      exit(EXIT_FAILURE);
    }
    
    if ( transformRepository->SetTransforms(trackedFrame) != PLUS_SUCCESS )
    {
      LOG_ERROR("Failed to update transforms in repository with tracked frame!");
      exit(EXIT_FAILURE);
    }

    bool valid = false;
    if ( (transformRepository->GetTransform(stylusToReferenceTransformName, stylusToReferenceMatrix, &valid) != PLUS_SUCCESS) || (!valid) )
    {
      LOG_ERROR("No valid transform found between stylus to reference!");
      exit(EXIT_FAILURE);
    }

    pivotCalibration->InsertNextCalibrationPoint(stylusToReferenceMatrix);
  }
  vtkPlusLogger::PrintProgressbar(100.0); 

  if (pivotCalibration->DoPivotCalibration(transformRepository) != PLUS_SUCCESS)
  {
    LOG_ERROR("Calibration error!");
    exit(EXIT_FAILURE);
  }

  // Save result
  if (transformRepository->WriteConfiguration(configRootElement) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to write pivot calibration result to configuration element!");
    exit(EXIT_FAILURE);
  }  

  std::string calibrationResultFileName = "StylusCalibrationTest.xml";
  LOG_INFO("Writing calibration result ("<<pivotCalibration->GetObjectPivotPointCoordinateFrame()<<" to "<<pivotCalibration->GetObjectMarkerCoordinateFrame()<<" transform) to "<<calibrationResultFileName);
  vtksys::SystemTools::RemoveFile(calibrationResultFileName.c_str());
  PlusCommon::PrintXML(calibrationResultFileName.c_str(), configRootElement); 
  
  if (!inputBaselineFileName.empty())
  {
    // Compare to baseline
    if ( CompareCalibrationResultsWithBaseline( inputBaselineFileName.c_str(), calibrationResultFileName.c_str(), pivotCalibration->GetObjectMarkerCoordinateFrame(), pivotCalibration->GetObjectPivotPointCoordinateFrame() ) !=0 )
    {
      LOG_ERROR("Comparison of calibration data to baseline failed");
      std::cout << "Exit failure!!!" << std::endl;
      return EXIT_FAILURE;
    }
  }
  else
  {
    LOG_DEBUG("Baseline file is not specified. Computed results are not compared to baseline results.");
  }

  std::cout << "Exit success!!!" << std::endl; 
  return EXIT_SUCCESS; 
}

//-----------------------------------------------------------------------------

// return the number of differences
int CompareCalibrationResultsWithBaseline(const char* baselineFileName, const char* currentResultFileName, const char* stylusCoordinateFrame, const char* stylusTipCoordinateFrame )
{
  int numberOfFailures=0;

  // Load current stylus calibration
  vtkSmartPointer<vtkXMLDataElement> currentRootElem = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(currentResultFileName));
  if (currentRootElem == NULL) 
  {  
    LOG_ERROR("Unable to read the current configuration file: " << currentResultFileName); 
    return ++numberOfFailures;
  }
  
  vtkSmartPointer<vtkMatrix4x4> transformCurrent = vtkSmartPointer<vtkMatrix4x4>::New(); 
  double currentError(0); 
  if ( vtkPlusConfig::ReadTransformToCoordinateDefinition(currentRootElem, stylusTipCoordinateFrame, stylusCoordinateFrame, transformCurrent, &currentError) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to read current pivot calibration result from configuration file!"); 
    return ++numberOfFailures;
  }

  // Load baseline stylus calibration
  vtkSmartPointer<vtkXMLDataElement> baselineRootElem = vtkSmartPointer<vtkXMLDataElement>::Take(vtkXMLUtilities::ReadElementFromFile(baselineFileName));
  if (baselineRootElem == NULL) 
  {  
    LOG_ERROR("Unable to read the baseline configuration file: " << baselineFileName); 
    return ++numberOfFailures;
  }

  vtkSmartPointer<vtkMatrix4x4> transformBaseline = vtkSmartPointer<vtkMatrix4x4>::New(); 
  double baselineError(0); 
  if ( vtkPlusConfig::ReadTransformToCoordinateDefinition(baselineRootElem, stylusTipCoordinateFrame, stylusCoordinateFrame, transformBaseline, &baselineError) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to read current stylus calibration result from configuration file!"); 
    return ++numberOfFailures;
  }

  // Compare the transforms
  double posDiff = PlusMath::GetPositionDifference(transformCurrent, transformBaseline); 
  double rotDiff = PlusMath::GetOrientationDifference(transformCurrent, transformBaseline); 

  std::ostringstream currentTransform; 
  transformCurrent->PrintSelf(currentTransform, vtkIndent(0));
  std::ostringstream baselineTransform; 
  transformBaseline->PrintSelf(baselineTransform, vtkIndent(0));

  if ( posDiff > TRANSLATION_ERROR_THRESHOLD)
  {
    LOG_ERROR("Transform mismatch: translation difference is " <<posDiff<< ", maximum allowed is "<<TRANSLATION_ERROR_THRESHOLD);
    LOG_INFO("Current transform: " << currentTransform.str());
    LOG_INFO("Baseline transform: " << baselineTransform.str());
    numberOfFailures++;
  }

  if (rotDiff > ROTATION_ERROR_THRESHOLD )
  {
    LOG_ERROR("Transform mismatch: rotation difference is " <<rotDiff<< ", maximum allowed is "<<TRANSLATION_ERROR_THRESHOLD);
    LOG_INFO("Current transform: " << currentTransform.str());
    LOG_INFO("Baseline transform: " << baselineTransform.str());
    numberOfFailures++;
  }

  return numberOfFailures;
}