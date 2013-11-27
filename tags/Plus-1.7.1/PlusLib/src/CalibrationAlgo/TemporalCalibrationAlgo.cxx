/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#include "vtkTransformRepository.h"
#include "TemporalCalibrationAlgo.h"
#include "vtkMath.h"
#include "RANSAC.h"
#include "PlaneParametersEstimator.h"

#include <iostream>
#include <fstream>



// Default algorithm parameters
// TODO: have separate SIGNAL P2P for video and tracker (one in mm the other in pixels)
static const double MINIMUM_SIGNAL_PEAK_TO_PEAK = 0.01; // If either tracker metric "swings" less than this, abort
static const double TIMESTAMP_EPSILON_SEC = 0.0001; // Temporal resolution below which two time values are considered identical
static const double MINIMUM_SAMPLING_RESOLUTION_SEC = 0.00001; // The maximum resolution that the user can request
static const double DEFAULT_SAMPLING_RESOLUTION_SEC = 0.001; 
static const double DEFAULT_MAX_TRACKER_LAG_SEC = 2;
static const std::string DEFAULT_PROBE_TO_REFERENCE_TRANSFORM_NAME = "ProbeToReference";
static const double IMAGE_DOWSAMPLING_FACTOR_X = 4; // new resolution_x = old resolution_x/ IMAGE_DOWSAMPLING_FACTOR_X
static const double IMAGE_DOWSAMPLING_FACTOR_Y = 4; // new resolution_y = old resolution_y/ IMAGE_DOWSAMPLING_FACTOR_Y
static const int NUMBER_OF_SCANLINES = 40; // number of scan-lines for line detection
static const unsigned int DIMENSION = 2; // dimension of video frames (used for Ransac plane)
static const int MINIMUM_NUMBER_OF_VALID_SCANLINES = 5; // minimum number of valid scanlines to compute line position
static const double INTESNITY_THRESHOLD_PERCENTAGE_OF_PEAK = 0.5; // threshold (as the percentage of the peak intensity along a scanline) for COG
static const double MAX_PERCENTAGE_OF_INVALID_VIDEO_FRAMES = 0.1; // the maximum percentage of the invalid frames before warning message issued
static const double MAX_CONSECUTIVE_INVALID_VIDEO_FRAMES = 10; // the maximum number of consecutive invalid frames before warning message issued
static const double MIN_X_SLOPE_COMPONENT_FOR_DETECTED_LINE = 0.01; // if the detected line's slope's x-component is less than this (i.e. almost vertical), skip frame

enum PEAK_POS_METRIC_TYPE
{
  PEAK_POS_COG,
  PEAK_POS_START
};
const bool PEAK_POS_METRIC = PEAK_POS_COG;

enum SIGNAL_ALIGNMENT_METRIC_TYPE
{
  SSD,
  CORRELATION,
  SAD,
  SIGNAL_METRIC_TYPE_COUNT,
};
const SIGNAL_ALIGNMENT_METRIC_TYPE SIGNAL_ALIGNMENT_METRIC = SSD;

static const double SIGNAL_ALIGNMENT_METRIC_THRESHOLD[SIGNAL_METRIC_TYPE_COUNT] = {-2^500, -2^500, 2^500};

enum METRIC_NORMALIZATION_TYPE
{
  STD,
  AMPLITUDE
};
const int METRIC_NORMALIZATION = AMPLITUDE;


//-----------------------------------------------------------------------------
TemporalCalibration::TemporalCalibration() : m_SamplingResolutionSec(DEFAULT_SAMPLING_RESOLUTION_SEC),
                                             m_MaxTrackerLagSec(DEFAULT_MAX_TRACKER_LAG_SEC),
                                             m_ProbeToReferenceTransformName(DEFAULT_PROBE_TO_REFERENCE_TRANSFORM_NAME),
                                             m_NeverUpdated(true),
                                             m_SaveIntermediateImages(false),
                                             m_TrackerLagSec(0.0),
                                             m_CalibrationError(0.0),
                                             m_BestCorrelationNormalizationFactor(0.0),
                                             m_VideoPositionMetricNormalizationFactor(0.0),
                                             m_BestCorrelationLagIndex(-1)
{
  /* TODO: Switching to VTK table data structure */
  m_TrackerTable = vtkSmartPointer<vtkTable>::New();
  m_VideoTable = vtkSmartPointer<vtkTable>::New();
  m_TrackerTimestampedMetric = vtkSmartPointer<vtkTable>::New();     
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::Update(TEMPORAL_CALIBRATION_ERROR &error)
{
  // Reset the error
  error = TEMPORAL_CALIBRATION_ERROR_NONE;

  // Check if video frames have been assigned
  if(m_VideoFrames == NULL)
  {
    error = TEMPORAL_CALIBRATION_ERROR_NO_VIDEO_DATA;
    LOG_ERROR("Video data is not assigned");
    return PLUS_FAIL;
  }

  // Check if TrackedFrameList is MF oriented BRIGHTNESS image
  if (vtkTrackedFrameList::VerifyProperties(this->m_VideoFrames, US_IMG_ORIENT_MF, US_IMG_BRIGHTNESS)!=PLUS_SUCCESS)
  {
    error = TEMPORAL_CALIBRATION_ERROR_NOT_MF_ORIENTATION;
    LOG_ERROR("Failed to perform calibration - video data is invalid"); 
    return PLUS_FAIL; 
  }

  // Make sure video frame list is not empty
  if(m_VideoFrames->GetNumberOfTrackedFrames() == 0)
  {
    error = TEMPORAL_CALIBRATION_ERROR_NO_FRAMES_IN_VIDEO_DATA;
    LOG_ERROR("Video data contains no frames");
    return PLUS_FAIL;
  }

  // Make sure tracker frame list is not empty
  if(m_TrackerFrames->GetNumberOfTrackedFrames() == 0)
  {
    error = TEMPORAL_CALIBRATION_ERROR_NO_FRAMES_IN_ULTRASOUND_DATA;
    LOG_ERROR("US tracker data contains no frames");
    return PLUS_FAIL;
  }

  // Check that all the image data is valid
  int totalNumberOfInvalidVideoFrames = 0;
  int greatestNumberOfConsecutiveInvalidVideoFrames = 0;
  int currentNumberOfConsecutiveInvalidVideoFrames = 0;
  for(int i = 0 ; i < m_VideoFrames->GetNumberOfTrackedFrames(); ++i)
  {
    if(!m_VideoFrames->GetTrackedFrame(i)->GetImageData()->IsImageValid())
    {
      ++totalNumberOfInvalidVideoFrames;
      ++currentNumberOfConsecutiveInvalidVideoFrames;
    }
    else
    {
      if(currentNumberOfConsecutiveInvalidVideoFrames > greatestNumberOfConsecutiveInvalidVideoFrames)
      {
        greatestNumberOfConsecutiveInvalidVideoFrames = currentNumberOfConsecutiveInvalidVideoFrames ;
      } 
      currentNumberOfConsecutiveInvalidVideoFrames = 0;
    }
  }

  double percentageOfInvalidVideoFrames  = totalNumberOfInvalidVideoFrames / static_cast<double>(m_VideoFrames->GetNumberOfTrackedFrames());
  if(percentageOfInvalidVideoFrames > MAX_PERCENTAGE_OF_INVALID_VIDEO_FRAMES)
  {
    LOG_WARNING(100*percentageOfInvalidVideoFrames << "% of the video frames were invalid. This warning " <<
      "gets issued whenever more than " << 100*MAX_PERCENTAGE_OF_INVALID_VIDEO_FRAMES << "% of the video frames are invalid because the " <<
      "accuracy of the computed time offset may be marginalised");
  }

  if(greatestNumberOfConsecutiveInvalidVideoFrames  > MAX_CONSECUTIVE_INVALID_VIDEO_FRAMES)
  {
    LOG_WARNING("There were " << greatestNumberOfConsecutiveInvalidVideoFrames  << " invalid video frames in a row. This warning " <<
      "gets issued whenever there are more than " << MAX_CONSECUTIVE_INVALID_VIDEO_FRAMES << " invalid frames in a row because the " <<
      "accuracy of the computed time offset may be marginalised");
  }

  if(m_SamplingResolutionSec < MINIMUM_SAMPLING_RESOLUTION_SEC)
  {
    error = TEMPORAL_CALIBRATION_ERROR_SAMPLING_RESOLUTION_TOO_SMALL;
    LOG_ERROR(
      "Specified resampling resolution (" 
      << m_SamplingResolutionSec 
      << " seconds) is too small. Sampling resolution must be greater than: " 
      << MINIMUM_SAMPLING_RESOLUTION_SEC 
      << " seconds");
    return PLUS_FAIL;
  }

  if(ComputeTrackerLagSec(error) != PLUS_SUCCESS)
  {
      return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::SetSaveIntermediateImagesToOn(bool saveIntermediateImages)
{
  m_SaveIntermediateImages = saveIntermediateImages;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::SetTrackerFrames(vtkTrackedFrameList* trackerFrames)
{
  m_TrackerFrames = trackerFrames;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::SetVideoFrames(vtkTrackedFrameList* videoFrames)
{
  m_VideoFrames = videoFrames;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::SetSamplingResolutionSec(double samplingResolutionSec)
{
  m_SamplingResolutionSec = samplingResolutionSec;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::SetProbeToReferenceTransformName(const std::string& transformName)
{
  m_ProbeToReferenceTransformName = transformName;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::SetMaximumVideoTrackerLagSec(double maxLagSec)
{
  m_MaxTrackerLagSec = maxLagSec;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::SetIntermediateFilesOutputDirectory(std::string &outputDirectory)
{
  m_IntermediateFilesOutputDirectory = outputDirectory;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetTrackerLagSec(double &lag)
{
  if(m_NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the tracker lag.");
    return PLUS_FAIL;
  }

  lag = m_TrackerLagSec;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetBestCorrelation( double &videoCorrelation )
{
  if(m_NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the best correlation metric.");
    return PLUS_FAIL;
  }

	videoCorrelation = m_BestCorrelationValue;

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetCalibrationError(double &error)
{
  if(m_NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the calibration error.");
    return PLUS_FAIL;
  }

  error = m_CalibrationError;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetMaxCalibrationError(double &maxCalibrationError)
{
  if(m_NeverUpdated)
  {
    LOG_ERROR("You must first call the \"Update()\" to compute the calibration error.");
    return PLUS_FAIL;
  }

  maxCalibrationError = m_MaxCalibrationError;
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetUncalibratedTrackerPositionSignal(vtkTable* uncalibratedTrackerPositionSignal)
{
  ConstructTableSignal(m_NormalizedTrackerTimestamps, m_NormalizedTrackerPositionMetric, uncalibratedTrackerPositionSignal, 0); 

  if(uncalibratedTrackerPositionSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold video signal. Table has " << 
               uncalibratedTrackerPositionSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }

  uncalibratedTrackerPositionSignal->GetColumn(0)->SetName("Time [s]");
  uncalibratedTrackerPositionSignal->GetColumn(1)->SetName("Uncalibrated Tracker Position Metric");

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetCalibratedTrackerPositionSignal(vtkTable* calibratedTrackerPositionSignal)
{
  ConstructTableSignal(m_NormalizedTrackerTimestamps, m_NormalizedTrackerPositionMetric, calibratedTrackerPositionSignal, -m_TrackerLagSec); 

  if(calibratedTrackerPositionSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold video signal. Table has " << 
               calibratedTrackerPositionSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }

  calibratedTrackerPositionSignal->GetColumn(0)->SetName("Time [s]");
  calibratedTrackerPositionSignal->GetColumn(1)->SetName("Calibrated Tracker Position Metric");

  return PLUS_SUCCESS;
}
//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetVideoPositionSignal(vtkTable *videoPositionSignal)
{
  ConstructTableSignal(m_SlidingSignalTimestamps, m_SlidingSignalMetric, videoPositionSignal, 0); 

  if(videoPositionSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold uncalibrated tracker signal. Table has " << 
               videoPositionSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }

  videoPositionSignal->GetColumn(0)->SetName("Time [s]");
  videoPositionSignal->GetColumn(1)->SetName("Video Position Metric");

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::GetCorrelationSignal(vtkTable* correlationSignal)
{
  ConstructTableSignal(m_CorrTimeOffsets, m_CorrValues, correlationSignal, 0); 

  if(correlationSignal->GetNumberOfColumns() != 2)
  {
    LOG_ERROR("Error in constructing the vtk tables that are to hold video signal. Table has " << 
               correlationSignal->GetNumberOfColumns() << " columns, but should have two columns");
    return PLUS_FAIL;
  }

  correlationSignal->GetColumn(0)->SetName("Time Offset [s]");
  correlationSignal->GetColumn(1)->SetName("Computed Correlation");

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::filterFrames()
{
  vtkSmartPointer<vtkTransformRepository> transformRepository = vtkSmartPointer<vtkTransformRepository>::New();
  PlusTransformName transformName;

  if (transformName.SetTransformName(m_ProbeToReferenceTransformName.c_str())!=PLUS_SUCCESS)
  {
    LOG_ERROR("Cannot compute tracker position metric, transform name is invalid ("<<m_ProbeToReferenceTransformName<<")");
    return PLUS_FAIL;
  }

  for(int frame = 0; frame < m_TrackerFrames->GetNumberOfTrackedFrames(); ++frame )
  {
    TrackedFrame *trackedFrame = m_TrackerFrames->GetTrackedFrame(frame);
    transformRepository->SetTransforms(*trackedFrame);
    vtkSmartPointer<vtkMatrix4x4> probeToReferenceTransform = vtkSmartPointer<vtkMatrix4x4>::New();
    bool valid = false;
    transformRepository->GetTransform(transformName, probeToReferenceTransform, &valid);
    if (!valid)
    {
      // There is no available transform for this frame; skip that frame
      continue;
    }
    else
    {
      m_TrackerTimestamps.push_back(trackedFrame->GetTimestamp());
    }

  }


  //  Find the time-range that is common to both tracker and image signals
  double translationTimestampMin = m_TrackerTimestamps.at(0);
  double translationTimestampMax = m_TrackerTimestamps.at(m_TrackerTimestamps.size() - 1);

  double imageTimestampMin = m_VideoTimestamps.at(0);
  double imageTimestampMax = m_VideoTimestamps.at(m_VideoTimestamps.size() - 1);

  m_CommonRangeMin = std::max(imageTimestampMin, translationTimestampMin); 
  m_CommonRangeMax = std::min(imageTimestampMax, translationTimestampMax);

  if (m_CommonRangeMin + m_MaxTrackerLagSec >= m_CommonRangeMax - m_MaxTrackerLagSec)
  {
    LOG_ERROR("Insufficient overlap between tracking data and image data to compute time offset"); 
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;

}
//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::ComputeTrackerPositionMetric(TEMPORAL_CALIBRATION_ERROR &error)
{
  vtkSmartPointer<vtkTransformRepository> transformRepository = vtkSmartPointer<vtkTransformRepository>::New();
  PlusTransformName transformName;

  if (transformName.SetTransformName(m_ProbeToReferenceTransformName.c_str())!=PLUS_SUCCESS)
  {
    error = TEMPORAL_CALIBRATION_ERROR_INVALID_TRANSFORM_NAME;
    LOG_ERROR("Cannot compute tracker position metric, transform name is invalid ("<<m_ProbeToReferenceTransformName<<")");
    return PLUS_FAIL;
  }

  // Clear the old tracker timestamps, preparing for an update
  m_TrackerTimestamps.clear();

  // Find the mean tracker position
  itk::Point<double, 3> trackerPositionSum;
  trackerPositionSum[0] = trackerPositionSum[1] = trackerPositionSum[2] = 0.0;
  std::vector<itk::Point<double, 3> > trackerPositions;
  int numberOfValidFrames = 0;
  for(int frame = 0; frame < m_TrackerFrames->GetNumberOfTrackedFrames(); ++frame )
  {
    TrackedFrame *trackedFrame = m_TrackerFrames->GetTrackedFrame(frame);
    transformRepository->SetTransforms(*trackedFrame);
    
    if(trackedFrame->GetTimestamp() > (m_CommonRangeMin + m_MaxTrackerLagSec) && trackedFrame->GetTimestamp() < m_CommonRangeMax - m_MaxTrackerLagSec)
    {

        //std::cout << "Tracked frame in desired time range" << std::endl;
        m_TrackerTimestamps.push_back(trackedFrame->GetTimestamp()); // These timestamps will be in the desired time range
        vtkSmartPointer<vtkMatrix4x4> probeToReferenceTransform = vtkSmartPointer<vtkMatrix4x4>::New();
        bool valid = false;
        transformRepository->GetTransform(transformName, probeToReferenceTransform, &valid);
        if (!valid)
        {
          // There is no available transform for this frame; skip that frame
          continue;
        }  
      
        //  Store current tracker position 
        itk::Point<double, 3> currTrackerPosition;
        currTrackerPosition[0] = probeToReferenceTransform->GetElement(0, 3);
        currTrackerPosition[1] = probeToReferenceTransform->GetElement(1, 3);
        currTrackerPosition[2] = probeToReferenceTransform->GetElement(2, 3);
        trackerPositions.push_back(currTrackerPosition);

        // Add current tracker position to the running total
        trackerPositionSum[0] = trackerPositionSum[0] + probeToReferenceTransform->GetElement(0, 3);
        trackerPositionSum[1] = trackerPositionSum[1] + probeToReferenceTransform->GetElement(1, 3);
        trackerPositionSum[2] = trackerPositionSum[2] + probeToReferenceTransform->GetElement(2, 3);
        ++numberOfValidFrames;
    }
  }

  // Calculate the principal axis of motion (using PCA)
  itk::Point<double,3> principalAxisOfMotion;
  ComputePrincipalAxis(trackerPositions, principalAxisOfMotion, numberOfValidFrames);

  // Compute the mean tracker poisition
  itk::Point<double, 3> meanTrackerPosition;
  meanTrackerPosition[0] = ( trackerPositionSum[0] / static_cast<double>(numberOfValidFrames) );
  meanTrackerPosition[1] = ( trackerPositionSum[1] / static_cast<double>(numberOfValidFrames) );
  meanTrackerPosition[2] = ( trackerPositionSum[2] / static_cast<double>(numberOfValidFrames) );

  // Project the mean tracker position on the prinicipal axis of motion
  double meanTrackerPositionProjection = meanTrackerPosition[0] * principalAxisOfMotion[0] 
                                       + meanTrackerPosition[1] * principalAxisOfMotion[1] 
                                       + meanTrackerPosition[2] * principalAxisOfMotion[2];


  //  For each tracker position in the recorded tracker sequence, get its translation from reference.
  for ( int frame = 0; frame < m_TrackerTimestamps.size(); ++frame )
  {
    // Project the current tracker position onto the principal axis of motion
    double currTrackerPositionProjection = trackerPositions.at(frame).GetElement(0) * principalAxisOfMotion[0]
                                         + trackerPositions.at(frame).GetElement(1) * principalAxisOfMotion[1]
                                         + trackerPositions.at(frame).GetElement(2) * principalAxisOfMotion[2];

    //  Store this translation and corresponding timestamp
    m_TrackerPositionMetric.push_back(currTrackerPositionProjection);
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::ComputePrincipalAxis(std::vector<itk::Point<double, 3> > &trackerPositions, 
                                               itk::Point<double,3> &principalAxisOfMotion, int numValidFrames)
{
  // Set the X-values
  const char m0Name[] = "M0";
  vtkSmartPointer<vtkDoubleArray> dataset1Arr = vtkSmartPointer<vtkDoubleArray>::New();
  dataset1Arr->SetNumberOfComponents(1);
  dataset1Arr->SetName( m0Name );
  for(int i = 0; i < numValidFrames; ++i)
  {
    dataset1Arr->InsertNextValue(trackerPositions[i].GetElement(0));
  }

  // Set the Y-values
  const char m1Name[] = "M1";
  vtkSmartPointer<vtkDoubleArray> dataset2Arr = vtkSmartPointer<vtkDoubleArray>::New();
  dataset2Arr->SetNumberOfComponents(1);
  dataset2Arr->SetName( m1Name );
  for(int i = 0; i < numValidFrames; ++i)
  {
    dataset2Arr->InsertNextValue(trackerPositions[i].GetElement(1));
  }

  // Set the Z-values
  const char m2Name[] = "M2";
  vtkSmartPointer<vtkDoubleArray> dataset3Arr = vtkSmartPointer<vtkDoubleArray>::New();
  dataset3Arr->SetNumberOfComponents(1);
  dataset3Arr->SetName( m2Name );
  for(int i = 0; i < numValidFrames; ++i)
  {
    dataset3Arr->InsertNextValue(trackerPositions[i].GetElement(2));
  }

  vtkSmartPointer<vtkTable> datasetTable = vtkSmartPointer<vtkTable>::New();
  datasetTable->AddColumn(dataset1Arr);
  datasetTable->AddColumn(dataset2Arr);
  datasetTable->AddColumn(dataset3Arr);

  vtkSmartPointer<vtkPCAStatistics> pcaStatistics = vtkSmartPointer<vtkPCAStatistics>::New();

#if VTK_MAJOR_VERSION <= 5
  pcaStatistics->SetInput( vtkStatisticsAlgorithm::INPUT_DATA, datasetTable );
#else
  pcaStatistics->SetInputData( vtkStatisticsAlgorithm::INPUT_DATA, datasetTable );
#endif
 
  pcaStatistics->SetColumnStatus("M0", 1 );
  pcaStatistics->SetColumnStatus("M1", 1 );
  pcaStatistics->SetColumnStatus("M2", 1 );
  pcaStatistics->RequestSelectedColumns();
  pcaStatistics->SetDeriveOption(true);
  pcaStatistics->Update();
 
  // Get the eigenvector corresponding to the largest eigenvalue (i.e. the principal axis). The
  // eigenvectors are stored with the eigenvector corresponding to the largest eigenvalue stored
  // first (i.e. in the "zero" position) and the eigenvector corresponding to the smallest eigenvalue
  // stored last. 
  vtkSmartPointer<vtkDoubleArray> eigenvector = vtkSmartPointer<vtkDoubleArray>::New();
  pcaStatistics->GetEigenvector(0, eigenvector);

  for(int i = 0; i < eigenvector->GetNumberOfComponents(); ++i)
  {
    principalAxisOfMotion[i] = eigenvector->GetComponent(0,i);
  }
}

//-----------------------------------------------------------------------------

PlusStatus TemporalCalibration::ComputeVideoPositionMetric(TEMPORAL_CALIBRATION_ERROR &error)
{
  //  For each video frame, detect line and extract mindpoint and slope parameters
  for(int frameNumber = 0; frameNumber < m_VideoFrames->GetNumberOfTrackedFrames(); ++frameNumber)
  {
    LOG_DEBUG("Calculating video position metric for frame " << frameNumber);

    // Define image types and image dimension
    const int imageDimension=2;
    typedef unsigned char charPixelType; // The natural type of the input image
    typedef float floatPixelType; //  The type of pixel used for the Hough accumulator
    typedef itk::Image<charPixelType,imageDimension> charImageType;

    // Get curent image
    charImageType::Pointer localImage = m_VideoFrames->GetTrackedFrame(frameNumber)->GetImageData()->GetImage<charPixelType>();

    if(localImage.IsNull())
    {
      // Dropped frame
      continue;
    }

    // Create an image duplicator to copy the original image
    typedef itk::ImageDuplicator<charImageType> DuplicatorType;
    DuplicatorType::Pointer duplicator = DuplicatorType::New();
    charImageType::Pointer scanlineImage;
      charImageType::Pointer IntensityPeakAndRansacLineImage;
    if(m_SaveIntermediateImages == true)
    {
      duplicator->SetInputImage(localImage);
      duplicator->Update();

      // Create an image copy to draw the scanlines on
      scanlineImage = duplicator->GetOutput();

      // Create an image copy to draw the detected intensity peaks and Ransac line
      IntensityPeakAndRansacLineImage = duplicator->GetOutput();
    }


    std::vector<itk::Point<double,2> > intensityPeakPositions;
    charImageType::RegionType region = localImage->GetLargestPossibleRegion();
    int numOfValidScanlines = 0;

    for(int currScanlineNum = 0; currScanlineNum < NUMBER_OF_SCANLINES; ++currScanlineNum)
    {
      // Set the scanline start pixel
      charImageType::IndexType startPixel;
      int scanlineSpacingPix = static_cast<int>(region.GetSize()[0] / (NUMBER_OF_SCANLINES + 1) );
      startPixel[0] = scanlineSpacingPix * (currScanlineNum + 1);
      startPixel[1] = 0;

      // Set the scanline end pixel
      charImageType::IndexType endPixel;
      endPixel[0] = startPixel[0];
      endPixel[1] = region.GetSize()[1] - 1;

      std::vector<int> intensityProfile; // Holds intensity profile of the line
      itk::LineIterator<charImageType> it(localImage, startPixel, endPixel); 
      it.GoToBegin();

      itk::LineIterator<charImageType> *itScanlineImage = NULL;
      if(m_SaveIntermediateImages == true)
      {
        // Iterator for the scanline image copy
        // TODO: explain that it's expensive to instantiate this object
        itScanlineImage = new itk::LineIterator<charImageType>(scanlineImage, startPixel, endPixel);
        itScanlineImage->GoToBegin();
      }

      while (!it.IsAtEnd())
      {
        intensityProfile.push_back((int)it.Get());
        if(m_SaveIntermediateImages == true)
        {
          // Set the pixels on the scanline image copy to white
          itScanlineImage->Set(255);
          ++(*itScanlineImage);
        }
        ++it;
      }

      // Delete the iterator declared with new()
      if(itScanlineImage != NULL)
      {
        delete itScanlineImage;
        itScanlineImage = NULL;
      }

      bool plotIntensityProfile = vtkPlusLogger::Instance()->GetLogLevel()>=vtkPlusLogger::LOG_LEVEL_TRACE;
      if(plotIntensityProfile)
      {
        // Plot the intensity profile
        plotIntArray(intensityProfile);
      }

      // Find the max intensity value from the peak with the largest area
      int MaxFromLargestArea = -1;
      int MaxFromLargestAreaIndex = -1;
      int startOfMaxArea = -1;
      if(FindLargestPeak(intensityProfile, MaxFromLargestArea, MaxFromLargestAreaIndex, startOfMaxArea) == PLUS_SUCCESS)
      {
        double currPeakPos_y = -1; 
        switch (PEAK_POS_METRIC)
        {
        case PEAK_POS_COG:
          {
            /* Use center-of-gravity (COG) as peak-position metric*/
            if(ComputeCenterOfGravity(intensityProfile, startOfMaxArea, currPeakPos_y) != PLUS_SUCCESS)
            {
              // unable to compute center-of-gravity; this scanline is invalid
              continue;
            }
            break;
          }
        case PEAK_POS_START:
          {
            /* Use peak start as peak-position metric*/
            if(FindPeakStart(intensityProfile,MaxFromLargestArea, startOfMaxArea, currPeakPos_y) != PLUS_SUCCESS)
            {
              // unable to compute peak start; this scanline is invalid
              continue;
            }
            break;
          }
        }

        itk::Point<double, 2> currPeakPos;
        currPeakPos[0] = static_cast<double>(startPixel[0]);
        currPeakPos[1] = currPeakPos_y;
        intensityPeakPositions.push_back(currPeakPos);
        ++numOfValidScanlines;

      } // end if() found intensity peak

    } // end currScanlineNum loop

    if(numOfValidScanlines < MINIMUM_NUMBER_OF_VALID_SCANLINES)
    {
      //TODO: drop the frame from the analysis
      LOG_DEBUG("Only " << numOfValidScanlines << " valid scanlines; this is less than the required " << MINIMUM_NUMBER_OF_VALID_SCANLINES << ". Skipping frame" << frameNumber);
    }

    std::vector<double> planeParameters;
    if(ComputeLineParameters(intensityPeakPositions, planeParameters) == PLUS_SUCCESS)
    {

      double r_x = - planeParameters.at(1);
      double r_y = planeParameters.at(0);
      double x_0 = planeParameters.at(2);
      double y_0 = planeParameters.at(3);

      if(r_x < MIN_X_SLOPE_COMPONENT_FOR_DETECTED_LINE)
      {
        // Line is close to vertical, skip frame because intersection of line with image's horizontal half point
        // is unstable
        continue;
      }

      // Store the y-value of the line, when the line's x-value is half of the image's width
      double t = ( 0.5 * region.GetSize()[0] - planeParameters.at(2) ) / r_x; 
      m_VideoPositionMetric.push_back( std::abs( planeParameters.at(3) + t * r_y ) );

      //  Store timestamp for image frame
      m_VideoTimestamps.push_back(m_VideoFrames->GetTrackedFrame(frameNumber)->GetTimestamp());

      if(m_SaveIntermediateImages == true)
      {
        // Write image showing the scan lines to file
        std::ostrstream scanLineImageFilename;
        scanLineImageFilename << m_IntermediateFilesOutputDirectory << "/scanLineImage" << std::setw(3) << std::setfill('0') << frameNumber << ".bmp" << std::ends;
        PlusVideoFrame::SaveImageToFile(scanlineImage, scanLineImageFilename.str());

        // Test writing of colour image to file
        typedef itk::RGBPixel<unsigned char> rgbPixelType;
        typedef itk::Image<rgbPixelType, 2> rgbImageType;
        rgbImageType::Pointer rgbImageCopy = rgbImageType::New();

        rgbImageType::IndexType start;
        start[0] =   0;  // first index on X
        start[1] =   0;  // first index on Y

        rgbImageType::SizeType  size;
        size[0]  = region.GetSize()[0];  // size along X
        size[1]  = region.GetSize()[1];  // size along Y

        rgbImageType::RegionType region;
        region.SetSize( size );
        region.SetIndex( start );

        rgbImageCopy->SetRegions( region );
        rgbImageCopy->Allocate();

        for(unsigned int x_coord = 0; x_coord  < region.GetSize()[0]; ++x_coord)
        {
          for(unsigned int y_coord = 0; y_coord < region.GetSize()[1]; ++y_coord)
          {
            rgbImageType::IndexType currRgbImageIndex;
            currRgbImageIndex[0] = x_coord;
            currRgbImageIndex[1] = y_coord;

            charImageType::IndexType currLocalImageIndex;
            currLocalImageIndex[0] = x_coord;
            currLocalImageIndex[1] = y_coord;

            charPixelType currLocalImagePixelVal = localImage->GetPixel(currLocalImageIndex);
            rgbPixelType currRgbImagePixelVal;
            currRgbImagePixelVal.Set(currLocalImagePixelVal, currLocalImagePixelVal, currLocalImagePixelVal);
            rgbImageCopy->SetPixel(currRgbImageIndex, currRgbImagePixelVal);
          }
        }

        float diag = vcl_sqrt((float)( size[0]*size[0] + size[1]*size[1] ));

        // Draw line
        for(int i = static_cast<int>(-diag); i < static_cast<int>(diag); ++i)
        {
          rgbImageType::IndexType currIndex;
          currIndex[0]= static_cast<int>(x_0 + i * r_x);
          currIndex[1]= static_cast<int>(y_0 + i * r_y);

          charImageType::RegionType outputRegion = localImage->GetLargestPossibleRegion();

          if(outputRegion.IsInside(currIndex))
          {
            rgbPixelType currRgbImagePixelVal;
            currRgbImagePixelVal.Set(0, 0, 255);
            rgbImageCopy->SetPixel(currIndex, currRgbImagePixelVal);
          }
        }

        // Draw intensity peaks (as squares)
        for(int i = 0; i < numOfValidScanlines; ++i)
        {
          unsigned int x_coord = static_cast<unsigned int>(intensityPeakPositions.at(i).GetElement(0));
          unsigned int y_coord = static_cast<unsigned int>(intensityPeakPositions.at(i).GetElement(1));

          for(int j = x_coord - 3; j < x_coord + 3; ++j)
          {
            for(int k = y_coord - 3; k < y_coord + 3; ++k)
            {
              rgbImageType::IndexType currIndex;
              currIndex[0] =  j;  // index on X
              currIndex[1] =  k;  // index on Y

              rgbPixelType currRgbImagePixelVal;
              currRgbImagePixelVal.Set(0, 0, 255);

              rgbImageCopy->SetPixel(currIndex, currRgbImagePixelVal);
            }
          }
        }

        std::ostrstream rgbImageFilename;
        rgbImageFilename << m_IntermediateFilesOutputDirectory << "/rgbImage" << std::setw(3) << std::setfill('0') << frameNumber << ".png" << std::ends;

        typedef itk::ImageFileWriter<rgbImageType> rgbImageWriterType;
        rgbImageWriterType::Pointer rgbImageWriter = rgbImageWriterType::New();

        rgbImageWriter->SetFileName(rgbImageFilename.str());
        rgbImageWriter->SetInput(rgbImageCopy);
        rgbImageWriter->Update();
      }// end writing color image

    }// end if compute line parameters is succesful

  }// end frameNum loop

  bool plotVideoMetric = vtkPlusLogger::Instance()->GetLogLevel()>=vtkPlusLogger::LOG_LEVEL_TRACE;
  if (plotVideoMetric)
  {
    plotDoubleArray(m_VideoPositionMetric);
  }

  return PLUS_SUCCESS;

} //  End LineDetection

//-----------------------------------------------------------------------------

PlusStatus TemporalCalibration::FindPeakStart(std::vector<int> &intensityProfile,int MaxFromLargestArea,
                                              int startOfMaxArea, double &startOfPeak)

{
  // Start of peak is defined as the location at which it reaches 50% of its maximum value.
  double startPeakValue = MaxFromLargestArea * 0.5;

  int pixelIndex = startOfMaxArea;

  while( intensityProfile.at(pixelIndex) <= startPeakValue)
  {
    ++pixelIndex;
  }
  
  startOfPeak = --pixelIndex;

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------

PlusStatus TemporalCalibration::FindLargestPeak(std::vector<int> &intensityProfile,int &MaxFromLargestArea,
                                                int &MaxFromLargestAreaIndex, int &startOfMaxArea)
{
  int currentLargestArea = 0;
  int currentArea = 0;
  int currentMaxFromLargestArea = 0;
  int currentMaxFromLargestAreaIndex = 0;
  int currentMax = 0;
  int currentMaxIndex = 0;
  bool underPeak = false;
  int currentStartOfMaxArea = 0;
  int currentStart = 0;

  if(intensityProfile.size() == 0 )
  {
    LOG_ERROR("Intensity contains no elements");
    return PLUS_FAIL;
  }

  double intensityMax = intensityProfile.at(0);
  for(int pixelLoc = 1; pixelLoc < intensityProfile.size(); ++pixelLoc)
  {
    if(intensityProfile.at(pixelLoc) > intensityMax)
    {
      intensityMax = intensityProfile.at(pixelLoc);
    }
  }

  double peakIntensityThreshold = intensityMax * INTESNITY_THRESHOLD_PERCENTAGE_OF_PEAK; 

  for(int pixelLoc = 0; pixelLoc < intensityProfile.size(); ++pixelLoc)
  {
    if(intensityProfile.at(pixelLoc) > peakIntensityThreshold  && !underPeak)
    {
      // reached start of the peak
      underPeak = true;
      currentMax = intensityProfile.at(pixelLoc);
      currentMaxIndex = pixelLoc;
      currentArea = intensityProfile.at(pixelLoc);
      currentStart = pixelLoc;
    }
    else if(intensityProfile.at(pixelLoc) > peakIntensityThreshold  && underPeak)
    {
      // still under the the peak, cumulate the area
      currentArea += intensityProfile.at(pixelLoc);
      
      if(intensityProfile.at(pixelLoc) > currentMax)
      {
        currentMax = intensityProfile.at(pixelLoc);
        currentMaxIndex = pixelLoc;
      }
    }
    else if(intensityProfile.at(pixelLoc) < peakIntensityThreshold && underPeak)
    {
      // exited the peak area
      underPeak = false;
      if(currentArea > currentLargestArea)
      {
        currentLargestArea = currentArea;
        currentMaxFromLargestArea = currentMax;
        currentMaxFromLargestAreaIndex = currentMaxIndex;
        currentStartOfMaxArea = currentStart;
      }
    }
  } //end loop through intensity profile

  MaxFromLargestArea = currentMaxFromLargestArea;
  MaxFromLargestAreaIndex = currentMaxFromLargestAreaIndex;
  startOfMaxArea = currentStartOfMaxArea;

  if(currentLargestArea == 0)
  {
    // No peak for this scanline
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}
//-----------------------------------------------------------------------------

PlusStatus TemporalCalibration::ComputeCenterOfGravity(std::vector<int> &intensityProfile, int startOfMaxArea, 
                                                       double &centerOfGravity)
{
  if(intensityProfile.size() == 0)
  {
    return PLUS_FAIL;
  }

  double intensityMax = intensityProfile.at(0);
  for(int pixelLoc = 1; pixelLoc < intensityProfile.size(); ++pixelLoc)
  {
    if(intensityProfile.at(pixelLoc) > intensityMax)
    {
      intensityMax = intensityProfile.at(pixelLoc);
    }
  }

  double peakIntensityThreshold = intensityMax * INTESNITY_THRESHOLD_PERCENTAGE_OF_PEAK; 


  int pixelLoc = startOfMaxArea;
  int pointsInPeak = 0;
  double intensitySum = 0;
  while(intensityProfile.at(pixelLoc) > peakIntensityThreshold)
  {
    intensitySum += pixelLoc * intensityProfile.at(pixelLoc);
    pointsInPeak += intensityProfile.at(pixelLoc);
    ++pixelLoc;
  }

  if(pointsInPeak == 0)
  {
    return PLUS_FAIL;
  }

  centerOfGravity = intensitySum / pointsInPeak;

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::NormalizeMetricWindow2(std::vector<double> &signal, double &normalizationFactor)
{

  if (signal.size() == 0)
  {
    LOG_ERROR("\"NormalizeMetricWindow2()\" failed because the metric vector is empty");
    return PLUS_FAIL;
  }

  //  Calculate the metric mean
  double mu = 0;
  for(int i = 0; i < signal.size(); ++i)
  {
    mu += signal.at(i);
  }

  mu /= signal.size();

  // Initialize standard deviation
  double s = 0;

  switch (METRIC_NORMALIZATION)
  {
    case AMPLITUDE:
    {
      // Do nothing
      break;
    }
    case STD:
    {
      // Calculate standard deviation
      for(int i = 0; i < signal.size(); ++i)
      {
        s += (signal.at(i) - mu)*(signal.at(i) - mu);
      }

      s = std::sqrt(s);
      s /= std::sqrt( static_cast<double>(signal.size()) - 1);
      break;
    } // End "case: STD"
  }



  //  Subtract the metric mean from each metric value as: s' = s - mu
  for(int i = 0; i < signal.size(); ++i)
  {
    signal.at(i) -= mu;
  }

  //  Calculate maximum and minimum metric values
  double maxMetricValue = signal.at(0);
  double minMetricValue = signal.at(0);

  for(int i = 1; i < signal.size(); ++i)
  {
    if(signal.at(i) > maxMetricValue)
    {
      maxMetricValue = signal.at(i);
    }
    else if(signal.at(i) < minMetricValue)
    {
       minMetricValue = signal.at(i);
    }
  }


  // If the metric values do not "swing" sufficiently, the signal is considered constant--i.e. infinite period--and will
  // not work for our purposes
  double maxPeakToPeak = std::abs(maxMetricValue) + std::abs(minMetricValue);

  if(maxPeakToPeak < MINIMUM_SIGNAL_PEAK_TO_PEAK)
  {
    LOG_ERROR("!!Detected metric values do not vary sufficiently--i.e. signal is constant");
    return PLUS_FAIL;
  }

  switch (METRIC_NORMALIZATION)
  {
    case AMPLITUDE:
    {
      // Divide by the maximum signal amplitude
      normalizationFactor = 1.0/maxPeakToPeak;
      for(int i = 0; i < signal.size(); ++i)
      {
        signal.at(i) *= normalizationFactor; 
      }
      break;
    } // End "case: AMPLITUDE" 
    case STD:
    {
      // Divide by the standard deviation
      normalizationFactor = 1.0/s;
      for(int i = 0; i < signal.size(); ++i)
      {
        signal.at(i) *= normalizationFactor; 
      } 
      break;
    } // End "case STD"
  }


  return PLUS_SUCCESS;
}// End NormalizeMetricWindow()

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::FilterPositionMetrics(TEMPORAL_CALIBRATION_ERROR &error)
{
  if (m_TrackerTimestamps.size()==0)
  {
    error = TEMPORAL_CALIBRATION_ERROR_NO_TIMESTAMPS;
    LOG_ERROR("FilterPositionMetrics failed, the TrackerTimestamps vector is empty");
    return PLUS_FAIL;
  }

	// Eliminate the tracker and video timestamps + metrics that are outside of the common range
	for(int i = 0; i < m_VideoTimestamps.size(); ++i)
	{
		if(m_VideoTimestamps.at(i) > m_CommonRangeMin && m_VideoTimestamps.at(i)  < m_CommonRangeMax)
		{
			m_FilteredVideoTimestamps.push_back(m_VideoTimestamps.at(i));
			m_FilteredVideoPositionMetric.push_back(m_VideoPositionMetric.at(i));
		}
	}

	for(int i = 0; i < m_TrackerTimestamps.size(); ++i)
	{
		if(m_TrackerTimestamps.at(i) > m_CommonRangeMin && m_TrackerTimestamps.at(i)  < m_CommonRangeMax)
		{
			m_FilteredTrackerTimestamps.push_back(m_TrackerTimestamps.at(i));
			m_FilteredTrackerPositionMetric.push_back(m_TrackerPositionMetric.at(i));
		}
	}


  return PLUS_SUCCESS;
}


//-----------------------------------------------------------------------------

PlusStatus TemporalCalibration::ResampleSignalLinearly(const std::vector<double>& templateSignalTimestamps,
																											 const std::vector<double>& origSignalTimestamps,
																											 const std::vector<double>& origSignalValues,
																											 std::vector<double>& resampledSignalValues)
{
	
	InterpolatePositionMetrics(origSignalTimestamps, origSignalValues, templateSignalTimestamps, resampledSignalValues, 0.5, 0);

	return PLUS_SUCCESS;

}

//-----------------------------------------------------------------------------

PlusStatus TemporalCalibration::InterpolatePositionMetrics(const std::vector<double> &originalTimestamps,
                                             const std::vector<double> &originalMetricValues,
                                             const std::vector<double> &resampledTimestamps,
                                             std::vector<double> &resampledPositionMetric,
                                             double midpoint, double sharpness)
{
  vtkSmartPointer<vtkPiecewiseFunction> piecewiseSignal = vtkSmartPointer<vtkPiecewiseFunction>::New();
  for(int i = 0; i < originalTimestamps.size(); ++i)
  {
    piecewiseSignal->AddPoint(originalTimestamps.at(i), originalMetricValues.at(i), midpoint, sharpness);
  }

  for(int i = 0; i < resampledTimestamps.size(); ++i)
  {
    resampledPositionMetric.push_back(piecewiseSignal->GetValue(resampledTimestamps.at(i)));
  }
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::ComputeCorrelationBetweenVideoAndTrackerMetrics2()
{
	//	We will let the tracker metric be the "metric" and let the video metric be the "sliding" metric. Since we are assuming a maximum offset between the two streams.

	m_SlidingSignalTimestamps.clear();
	m_SlidingSignalMetric.clear();
	for(int i = 0; i < m_FilteredVideoPositionMetric.size(); ++i)
	{
		if(m_FilteredVideoTimestamps.at(i) > m_CommonRangeMin + m_MaxTrackerLagSec && m_FilteredVideoTimestamps.at(i) < m_CommonRangeMax - m_MaxTrackerLagSec)
		{
			m_SlidingSignalTimestamps.push_back(m_FilteredVideoTimestamps.at(i));
			m_SlidingSignalMetric.push_back(m_FilteredVideoPositionMetric.at(i));
		}
	}
 
  NormalizeMetricWindow2(m_SlidingSignalMetric, m_VideoPositionMetricNormalizationFactor);

	//TODO: Check divide by zero!
	int offsetNum_max = vtkMath::Floor(m_MaxTrackerLagSec / m_SamplingResolutionSec);
	int offsetNum_min = -offsetNum_max;
	std::vector<double> slidingSignalTimestampsCopy(m_SlidingSignalTimestamps);
	for(int i = 0; i < slidingSignalTimestampsCopy.size(); ++i)
	{
		slidingSignalTimestampsCopy.at(i) += offsetNum_min * m_SamplingResolutionSec;
	}
	for(int offsetNum = offsetNum_min; offsetNum <= offsetNum_max; ++offsetNum)
	{
		std::vector<double> resampledTrackerPositionMetric;
		
		
		//ResampleTrackerPositionMetrics(slidingSignalTimestampsCopy, resampledTrackerPositionMetric);
		ResampleSignalLinearly(slidingSignalTimestampsCopy,m_FilteredTrackerTimestamps, 
													 m_FilteredTrackerPositionMetric,resampledTrackerPositionMetric);
		double normalizationFactor;
		NormalizeMetricWindow2(resampledTrackerPositionMetric, normalizationFactor);
		m_NormalizationFactors.push_back(normalizationFactor);

		switch (SIGNAL_ALIGNMENT_METRIC)
		{
		case SSD:
			{
				// Use sum of squared differences as signal alignment metric
				m_CorrValues.push_back(ComputeSsd(m_SlidingSignalMetric, resampledTrackerPositionMetric));
				break;
			}
		case CORRELATION:
			{
				// Use correlation as signal alignment metric
				m_CorrValues.push_back(ComputeCrossCorrelationSum(m_SlidingSignalMetric, resampledTrackerPositionMetric));
				break;
			}
		case SAD:
			{
				// Use sum of absolute differences as signal alignment metric
				m_CorrValues.push_back(ComputeSad(m_SlidingSignalMetric, resampledTrackerPositionMetric));
				break;
			}
		}

		m_CorrTimeOffsets.push_back(offsetNum * m_SamplingResolutionSec);
		for(int i = 0; i < slidingSignalTimestampsCopy.size(); ++i)
		{
			slidingSignalTimestampsCopy.at(i) += m_SamplingResolutionSec;
		}

	}

		
}

//-----------------------------------------------------------------------------
double TemporalCalibration::ComputeCrossCorrelationSum(const std::vector<double> &signalA, const std::vector<double> &signalB)
{
  if (signalA.size() != signalB.size() )
  { 
		//TODO: Update this error message!
    LOG_ERROR("");
    return 0;
  }

  double xCorrSum = 0;
  for(long int i = 0; i < signalA.size(); ++i)
  {
    xCorrSum += signalA.at(i) * signalB.at(i); // XCORR
  }
  return xCorrSum;
}

//-----------------------------------------------------------------------------
double TemporalCalibration::ComputeSsd(const std::vector<double> &signalA, const std::vector<double> &signalB)
{
  if (signalA.size() != signalB.size())
  { 
		//TODO: Update this error message!
    LOG_ERROR("");
    return 0;
  }

  double ssdSum = 0;
  for(long int i = 0; i < signalA.size(); ++i)
  {
    double diff = signalA.at(i) - signalB.at(i ); //SSD
    ssdSum -= diff*diff;
  }
  return ssdSum;
}

//-----------------------------------------------------------------------------
double TemporalCalibration::ComputeSad(const std::vector<double> &signalA, const std::vector<double> &signalB)
{

  if (signalA.size() != signalB.size() )
  { 
		//TODO: Update this error message!
    LOG_ERROR("");
    return 0;
  }

  double sadSum = 0;
  for(long int i = 0; i < signalA.size(); ++i)
  {
    sadSum -= fabs(signalA.at(i) - signalB.at(i)); //SAD
  }
  return sadSum;
}

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::ComputeTrackerLagSec(TEMPORAL_CALIBRATION_ERROR &error)
{

  // Calculate the metric for the video data
  ComputeVideoPositionMetric(error);
  
  // Get the time-range for the tracker signal
  filterFrames();

  // Calculate the metric for the tracker data
  if(ComputeTrackerPositionMetric(error) != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  // Eliminate the video and tracker data that is not within the overlapping timeframe
  if(FilterPositionMetrics(error) != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  //  Compute cross correlation with sign convention #1 
  LOG_TRACE("ComputeCorrelationBetweenVideoAndTrackerMetrics(sign convention #1)");
  ComputeCorrelationBetweenVideoAndTrackerMetrics2();
  
  //  Make a copy of the correlation values for sign convention #1
  std::vector<double> corrValsCopy;
  for(long int i = 0; i < m_CorrValues.size(); ++i)
  {
    corrValsCopy.push_back(m_CorrValues.at(i));
  }

  //  Make a copy of the correlation offsets for sign convention #1
  std::vector<double> corrTimeOffsetsCopy;
  for(long int i = 0; i < m_CorrTimeOffsets.size(); ++i)
  {
    corrTimeOffsetsCopy.push_back(m_CorrTimeOffsets.at(i));
  }

	  //  Make a copy of the normalization values for sign convention #1
  std::vector<double> normalizationFactorsCopy;
  for(long int i = 0; i < m_NormalizationFactors.size(); ++i)
  {
    normalizationFactorsCopy.push_back(m_NormalizationFactors.at(i));
  }
  
  // Find the index offset corresponding to the maximum correlation sum for sign convention #1
  if(m_CorrValues.size() == 0)
  {
    error = TEMPORAL_CALIBRATION_ERROR_CORRELATION_RESULT_EMPTY;
    LOG_ERROR("Correlation result list is empty.");
    return PLUS_FAIL;
  }

  double bestCorrelationValueSignConvention1 = m_CorrValues.at(0);
  double bestCorrelationTimeOffsetSignConvention1 = m_CorrTimeOffsets.at(0);
	double bestCorrelationNormalizationFactorSignConvention1 = m_NormalizationFactors.at(0);
  for(int i = 1; i < m_CorrValues.size(); ++i)
  {
    if(m_CorrValues.at(i) > bestCorrelationValueSignConvention1)
    {
      bestCorrelationValueSignConvention1 = m_CorrValues.at(i);
      bestCorrelationTimeOffsetSignConvention1  = m_CorrTimeOffsets.at(i);
			bestCorrelationNormalizationFactorSignConvention1 = m_NormalizationFactors.at(i);
    }
  }

  // Compute the time that the tracker data lags the video data using the maximum index( with sign convention #1)
  double trackerLagSecSignConvention1 = bestCorrelationTimeOffsetSignConvention1;
  LOG_DEBUG("Time offset with sign convention #1: " << trackerLagSecSignConvention1);

  //  Compute cross correlation with sign convention #2
  LOG_TRACE("ComputeCorrelationBetweenVideoAndTrackerMetrics(sign convention #2)");

  // Mirror tracker metric signal about x-axis 
  for(long int i = 0; i < m_FilteredTrackerPositionMetric.size(); ++i)
  {
    m_FilteredTrackerPositionMetric.at(i) *= -1;
  }

  // clear the old correlation values and indices
  m_CorrValues.clear();
  m_CorrTimeOffsets.clear();
	m_NormalizationFactors.clear();

  ComputeCorrelationBetweenVideoAndTrackerMetrics2();

  double bestCorrelationValueSignConvention2 = m_CorrValues.at(0);
  double bestCorrelationTimeOffsetSignConvention2 = m_CorrTimeOffsets.at(0);
  double bestCorrelationNormalizationFactorSignConvention2 = m_NormalizationFactors.at(0);
  for(int i = 1; i < m_CorrValues.size(); ++i)
  {
    if(m_CorrValues.at(i) > bestCorrelationValueSignConvention2)
    {
      bestCorrelationValueSignConvention2 = m_CorrValues.at(i);
      bestCorrelationTimeOffsetSignConvention2 = m_CorrTimeOffsets.at(i);
			bestCorrelationNormalizationFactorSignConvention2 = m_NormalizationFactors.at(i);
			
    }
  }

  // Compute the time that the tracker data lags the video data using the maximum index( with sign convention #2)
  double trackerLagSecSignConvention2 = bestCorrelationTimeOffsetSignConvention2;
  LOG_DEBUG("Time offset with sign convention #2: " << trackerLagSecSignConvention2);
  
	// Adopt the smallest tracker lag
  if(std::abs(trackerLagSecSignConvention1) < std::abs(trackerLagSecSignConvention2))
  {
    m_TrackerLagSec = trackerLagSecSignConvention1;
    m_BestCorrelationTimeOffset = bestCorrelationTimeOffsetSignConvention1;
    m_BestCorrelationValue = bestCorrelationValueSignConvention1;
		m_BestCorrelationNormalizationFactor = bestCorrelationNormalizationFactorSignConvention1;

    // Flip tracker metric signal back to correspond to sign convention #1 
    for(long int i = 0; i < m_FilteredTrackerPositionMetric.size(); ++i)
    {
      m_FilteredTrackerPositionMetric.at(i) *= -1;
    }

    // Switch to correlation values back to correspond to sign convention #1 
    m_CorrValues.clear();
    for(long int i = 0; i < corrValsCopy.size(); ++i)
    {
      m_CorrValues.push_back(corrValsCopy.at(i));
    }

    // Switch to time offsets back to correspond to sign convention #1 
    m_CorrTimeOffsets.clear();
    for(long int i = 0; i < corrTimeOffsetsCopy.size(); ++i)
    {
      m_CorrTimeOffsets.push_back(corrTimeOffsetsCopy.at(i));
    }

		// Switch to normalization factors back to correspond to sign convention #1
		m_NormalizationFactors.clear();
    for(long int i = 0; i < corrTimeOffsetsCopy.size(); ++i)
    {
      m_NormalizationFactors.push_back(normalizationFactorsCopy.at(i));
    }
  }
  else
  {
		m_BestCorrelationTimeOffset = bestCorrelationTimeOffsetSignConvention2;
    m_BestCorrelationValue = bestCorrelationValueSignConvention2;
    m_TrackerLagSec = trackerLagSecSignConvention2;
		m_BestCorrelationNormalizationFactor = bestCorrelationNormalizationFactorSignConvention2;
  }

	 

  // Normalize the video metric based on the best index offset (only considering the overlap "window"
	for(int i = 0; i < m_FilteredTrackerTimestamps.size(); ++i)
	{
		if(m_FilteredTrackerTimestamps.at(i) > m_SlidingSignalTimestamps.at(0) + m_TrackerLagSec && m_FilteredTrackerTimestamps.at(i) < m_SlidingSignalTimestamps.at(m_SlidingSignalTimestamps.size() -1) + m_TrackerLagSec)
		{
			m_NormalizedTrackerPositionMetric.push_back(m_FilteredTrackerPositionMetric.at(i));
			m_NormalizedTrackerTimestamps.push_back(m_FilteredTrackerTimestamps.at(i));
		}
	}

	// Get a normalized tracker position metric that can be displayed
	double unusedNormFactor;
	NormalizeMetricWindow2(m_NormalizedTrackerPositionMetric, unusedNormFactor);

	m_CalibrationError=sqrt(-m_BestCorrelationValue)/m_BestCorrelationNormalizationFactor; // RMSE in mm

  LOG_DEBUG("Tracker stream lags image stream by: " << m_TrackerLagSec << " [s]");


  // Get maximum calibration error

	//	Get the timestamps of the sliding signal (i.e. cropped video signal) shifted by the best-found offset
	std::vector<double> shiftedSlidingSignalTimestamps;
	for(int i = 0; i < m_SlidingSignalTimestamps.size(); ++i)
	{
		shiftedSlidingSignalTimestamps.push_back(m_SlidingSignalTimestamps.at(i) + m_TrackerLagSec);
	}

	//	Get the values of the tracker metric at the offset sliding signal values
	std::vector<double> resampledNormalizedTrackerPositionMetric;
	ResampleSignalLinearly(shiftedSlidingSignalTimestamps,m_NormalizedTrackerTimestamps,m_NormalizedTrackerPositionMetric,
												 resampledNormalizedTrackerPositionMetric);

  for(long int i = 0; i < resampledNormalizedTrackerPositionMetric.size(); ++i)
  {
    double diff = resampledNormalizedTrackerPositionMetric.at(i) - m_SlidingSignalMetric.at(i); //SSD
    m_CalibrationErrorVector.push_back(diff*diff); 
  }

  m_MaxCalibrationError = 0;
  for(long int i = 0; i < m_CalibrationErrorVector.size(); ++i)
  {
    if(m_CalibrationErrorVector.at(i) > m_MaxCalibrationError)
    {
      m_MaxCalibrationError = m_CalibrationErrorVector.at(i);
    }
  }

  m_MaxCalibrationError = std::sqrt(m_MaxCalibrationError)/m_BestCorrelationNormalizationFactor;


  m_NeverUpdated = false;

  if( m_BestCorrelationValue <= SIGNAL_ALIGNMENT_METRIC_THRESHOLD[SIGNAL_ALIGNMENT_METRIC] )
  {
    error = TEMPORAL_CALIBRATION_ERROR_RESULT_ABOVE_THRESHOLD;
    LOG_ERROR("Calculated correlation exceeds threshold value. This may be an indicator of a poor calibration.");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::plotIntArray(std::vector<int> intensityValues)
{
  //  Create table
  vtkSmartPointer<vtkTable> table = vtkSmartPointer<vtkTable>::New();

  //  Create array correpsonding to the time values of the tracker plot
  vtkSmartPointer<vtkIntArray> arrPixelPositions = vtkSmartPointer<vtkIntArray>::New();
  arrPixelPositions->SetName("Pixel Positions"); 
  table->AddColumn(arrPixelPositions);
 
  //  Create array corresponding to the metric values of the tracker plot
  vtkSmartPointer<vtkIntArray> arrIntensityProfile = vtkSmartPointer<vtkIntArray>::New();
  arrIntensityProfile->SetName("Intensity Profile");
  table->AddColumn(arrIntensityProfile);
 
  // Set the tracker data
  table->SetNumberOfRows(intensityValues.size());
  for (int i = 0; i < intensityValues.size(); ++i)
  {
    table->SetValue(i, 0, i);
    table->SetValue(i, 1, (intensityValues.at(i)));
  }

  // Set up the view
  vtkSmartPointer<vtkContextView> view = vtkSmartPointer<vtkContextView>::New();
  view->GetRenderer()->SetBackground(1.0, 1.0, 1.0);
 
  // Add the two line plots
  vtkSmartPointer<vtkChartXY> chart =  vtkSmartPointer<vtkChartXY>::New();
  view->GetScene()->AddItem(chart);
  vtkPlot *line = chart->AddPlot(vtkChart::LINE);

  #if VTK_MAJOR_VERSION <= 5
    line->SetInput(table, 0, 1);
  #else
    line->SetInputData(table, 0, 1);
  #endif

  line->SetColor(0, 255, 0, 255);
  line->SetWidth(1.0);
  line = chart->AddPlot(vtkChart::LINE);

  // Start interactor
  view->GetInteractor()->Initialize();
  view->GetInteractor()->Start();

} //  End plot()

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::ConstructTableSignal(std::vector<double> &x, std::vector<double> &y, vtkTable* table,
                                               double timeCorrection)
{
  // Clear table
  while (table->GetNumberOfColumns() > 0)
  {
    table->RemoveColumn(0);
  }

  //  Create array correpsonding to the time values of the tracker plot
  vtkSmartPointer<vtkDoubleArray> arrX = vtkSmartPointer<vtkDoubleArray>::New();
  table->AddColumn(arrX);
 
  //  Create array corresponding to the metric values of the tracker plot
  vtkSmartPointer<vtkDoubleArray> arrY = vtkSmartPointer<vtkDoubleArray>::New();
  table->AddColumn(arrY);
 
  // Set the tracker data
  table->SetNumberOfRows(x.size());
  for (int i = 0; i < x.size(); ++i)
  {
    table->SetValue(i, 0, x.at(i) + timeCorrection );
    table->SetValue(i, 1, y.at(i) );
  }

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
void TemporalCalibration::plotDoubleArray(std::vector<double> intensityValues)
{
  //  Create table
  vtkSmartPointer<vtkTable> table = vtkSmartPointer<vtkTable>::New();

  //  Create array correpsonding to the time values of the tracker plot
  vtkSmartPointer<vtkDoubleArray> arrPixelPositions = vtkSmartPointer<vtkDoubleArray>::New();
  arrPixelPositions->SetName("Pixel Positions"); 
  table->AddColumn(arrPixelPositions);
 
  //  Create array corresponding to the metric values of the tracker plot
  vtkSmartPointer<vtkDoubleArray> arrIntensityProfile = vtkSmartPointer<vtkDoubleArray>::New();
  arrIntensityProfile->SetName("Intensity Profile");
  table->AddColumn(arrIntensityProfile);
 
  // Set the tracker data
  table->SetNumberOfRows(intensityValues.size());
  for (int i = 0; i < intensityValues.size(); ++i)
  {
    table->SetValue(i, 0, i);
    table->SetValue(i, 1, (intensityValues.at(i)));
  }

  // Set up the view
  vtkSmartPointer<vtkContextView> view = vtkSmartPointer<vtkContextView>::New();
  view->GetRenderer()->SetBackground(1.0, 1.0, 1.0);
 
  // Add the two line plots
  vtkSmartPointer<vtkChartXY> chart =  vtkSmartPointer<vtkChartXY>::New();
  view->GetScene()->AddItem(chart);
  vtkPlot *line = chart->AddPlot(vtkChart::LINE);

  #if VTK_MAJOR_VERSION <= 5
    line->SetInput(table, 0, 1);
  #else
    line->SetInputData(table, 0, 1);
  #endif

  line->SetColor(0, 255, 0, 255);
  line->SetWidth(1.0);
  line = chart->AddPlot(vtkChart::LINE);

  // Start interactor
  view->GetInteractor()->Initialize();
  view->GetInteractor()->Start();

} //  End plot()

//-----------------------------------------------------------------------------
PlusStatus TemporalCalibration::ComputeLineParameters(std::vector<itk::Point<double,2> > &data, std::vector<double> &planeParameters)
{

  typedef itk::PlaneParametersEstimator<DIMENSION> PlaneEstimatorType;
  typedef itk::RANSAC<itk::Point<double, DIMENSION>, double> RANSACType;

  //create and initialize the parameter estimator
  double maximalDistanceFromPlane = 0.5;
  PlaneEstimatorType::Pointer planeEstimator = PlaneEstimatorType::New();
  planeEstimator->SetDelta( maximalDistanceFromPlane );
  planeEstimator->LeastSquaresEstimate( data, planeParameters );
  if( planeParameters.empty() )
  {
    LOG_ERROR("Unable to fit line through points with least squares estimation");
  }
  else
  {
    LOG_DEBUG("Least squares line parameters (n, a):");
    for(int i=0; i<(2*DIMENSION-1); i++ )
    {
      LOG_DEBUG(" LS parameter: "<<planeParameters[i]);
    }      
  }

  //create and initialize the RANSAC algorithm
  double desiredProbabilityForNoOutliers = 0.999;
  RANSACType::Pointer ransacEstimator = RANSACType::New();
  
  
  try{
    ransacEstimator->SetData( data );
  }
  catch( std::exception& e) {
    LOG_ERROR(e.what());
    return PLUS_FAIL;
  }

  try{
    ransacEstimator->SetParametersEstimator( planeEstimator.GetPointer() );
  }
  catch( std::exception& e) {
    LOG_ERROR(e.what());
    return PLUS_FAIL;
  }

  
  try{
    ransacEstimator->Compute( planeParameters, desiredProbabilityForNoOutliers );
  }
  catch( std::exception& e) {
    LOG_ERROR(e.what());
    return PLUS_FAIL;
  }
  
  if( planeParameters.empty() )
  {
    LOG_ERROR("Unable to fit line through points with RANSAC, temporal calibration failed");
    return PLUS_FAIL;
  }

  LOG_DEBUG("RANSAC line fitting parameters (n, a):");

  for(int i=0; i<(2*DIMENSION-1); i++ )
  {
    LOG_DEBUG(" RANSAC parameter: " << planeParameters[i]);
  }    

  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
