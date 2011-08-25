#include "PlusConfigure.h"
#include "vtkCalibrationController.h"

#include "vtkObjectFactory.h"
#include "vtkImageExport.h"
#include "vtkMatrix4x4.h"
#include "vtkDirectory.h"
#include "vtkImageImport.h"
#include "vtkConfigurationTools.h"

#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkMetaImageSequenceIO.h"
#include <itkImageDuplicator.h>
#include "vtkTransform.h"
#include "vtkMath.h"
#include "vtkTriangle.h"
#include "vtkPlane.h"
#include "vtksys/SystemTools.hxx"

vtkCxxRevisionMacro(vtkCalibrationController, "$Revision: 1.0 $");
vtkStandardNewMacro(vtkCalibrationController);

//----------------------------------------------------------------------------
void vtkCalibrationController::PrintSelf(ostream& os, vtkIndent indent)
{
	this->Superclass::PrintSelf(os,indent);
} 

//----------------------------------------------------------------------------
vtkCalibrationController::vtkCalibrationController() 
: 
SegmentationProgressCallbackFunction(NULL)
{
	this->EnableTrackedSequenceDataSavingOff();
	this->EnableErroneouslySegmentedDataSavingOff(); 
	this->EnableSegmentationAnalysisOff();
  this->EnableVisualizationOn(); 
	this->InitializedOff(); 
  this->CalibrationDoneOff(); 

	this->OutputPath = NULL; 
	this->ProgramFolderPath = NULL; 
	this->ConfigurationFileName = NULL;
	this->ModelToPhantomTransform = NULL;
	this->PhantomModelFileName = NULL;
  this->OfflineImageData = NULL;
  
  this->ConfigurationData = NULL;
  
  this->CalibrationDate = NULL; 
  
  this->OfflineImageData = vtkImageData::New(); 
	
	this->SetCalibrationMode(REALTIME); 

	// Set program folder path to current working directory by default
	this->SetProgramFolderPath(vtksys::SystemTools::GetCurrentWorkingDirectory().c_str()); 

	for ( int i = 0; i < NUMBER_OF_IMAGE_DATA_TYPES; i++ )
	{
		vtkTrackedFrameList *trackedFrameList = vtkTrackedFrameList::New(); 
		this->TrackedFrameListContainer.push_back(trackedFrameList); 

		ImageDataInfo imageDataInfo; 
		imageDataInfo.NumberOfImagesToAcquire = 0; 
		imageDataInfo.NumberOfSegmentedImages = 0; 
		imageDataInfo.OutputSequenceMetaFileSuffix.clear(); 
    imageDataInfo.InputSequenceMetaFileName.clear(); 
		ImageDataInfoContainer.push_back(imageDataInfo); 
	}
}

//----------------------------------------------------------------------------
vtkCalibrationController::~vtkCalibrationController() 
{
	for ( int i = 0; i < NUMBER_OF_IMAGE_DATA_TYPES; i++ )
	{
		if ( this->TrackedFrameListContainer[i] != NULL )
		{
			this->TrackedFrameListContainer[i]->Delete();  
			this->TrackedFrameListContainer[i] = NULL; 
		}
	}

  for ( int i = 0; i < this->SegmentedFrameContainer.size(); i++ )
  {
    if ( this->SegmentedFrameContainer[i].TrackedFrameInfo != NULL )
    {
      delete this->SegmentedFrameContainer[i].TrackedFrameInfo; 
      this->SegmentedFrameContainer[i].TrackedFrameInfo = NULL; 
    }
  }

  this->SetConfigurationData(NULL);

  if ( this->OfflineImageData != NULL )
  {
    this->OfflineImageData->Delete(); 
    this->OfflineImageData = NULL; 
  }
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::Initialize()
{
	LOG_TRACE("vtkCalibrationController::Initialize"); 

	// Initialize the segmenation component
	// ====================================
  this->PatternRecognition.ReadConfiguration(this->GetConfigurationData());

	return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
vtkTrackedFrameList* vtkCalibrationController::GetTrackedFrameList( IMAGE_DATA_TYPE dataType )
{
	LOG_TRACE("vtkCalibrationController::GetTrackedFrameList"); 
	return this->TrackedFrameListContainer[dataType]; 
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::SaveTrackedFrameListToMetafile( IMAGE_DATA_TYPE dataType, const char* outputFolder, const char* sequenceMetafileName, bool useCompression /*= false*/ )
{
  LOG_TRACE("vtkCalibrationController::SaveTrackedFrameListToMetafile"); 
  if ( this->TrackedFrameListContainer[dataType]->SaveToSequenceMetafile(outputFolder, sequenceMetafileName, vtkTrackedFrameList::SEQ_METAFILE_MHA, useCompression) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to save tracked frames to sequence metafile!"); 
    return PLUS_FAIL;
  }

  this->TrackedFrameListContainer[dataType]->Clear(); 

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::AddVtkImageData(vtkImageData* frame, vtkMatrix4x4* trackingTransform, IMAGE_DATA_TYPE dataType )
{
	LOG_TRACE("vtkCalibrationController::AddData - vtkImage"); 
	ImageType::Pointer exportedFrame = ImageType::New();
    if ( UsImageConverterCommon::ConvertVtkImageToItkImage(frame, exportedFrame) != PLUS_SUCCESS )
    {
        LOG_ERROR("Failed to convert vtk image to itk image!"); 
        return PLUS_FAIL; 
    }
	return this->AddItkImageData(exportedFrame, trackingTransform, dataType); 
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::AddItkImageData(const ImageType::Pointer& frame, vtkMatrix4x4* trackingTransform, IMAGE_DATA_TYPE dataType )
{
	LOG_TRACE("vtkCalibrationController::AddData - itkImage"); 
	TrackedFrame trackedFrame; 
	this->CreateTrackedFrame(frame, trackingTransform, dataType, trackedFrame); 
	return this->AddTrackedFrameData(&trackedFrame, dataType); 
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::AddTrackedFrameData(TrackedFrame* trackedFrame, IMAGE_DATA_TYPE dataType )
{
	LOG_TRACE("vtkCalibrationController::AddData - TrackedFrame"); 
	try
	{
		// Check to see if the segmentation has returned the targets
    if ( (trackedFrame->ImageData.GetPointer() != NULL) && (this->SegmentImage(trackedFrame->ImageData) != PLUS_SUCCESS) )
    {
        LOG_WARNING("Undefined error occured during frame segmentation!"); 
        return PLUS_FAIL; 
    }

		// Add frame to the container 
		int trackedFramePosition(-1); 
		if ( this->EnableErroneouslySegmentedDataSaving )
		{
			// Save the erroneously segmented frames too 
			trackedFramePosition = this->TrackedFrameListContainer[dataType]->AddTrackedFrame(trackedFrame); 
		}
		else if (this->GetPatRecognitionResult()->GetDotsFound() )
		{
			// Segmentation was successful
			trackedFramePosition = this->TrackedFrameListContainer[dataType]->AddTrackedFrame(trackedFrame); 
		}

		// Draw segmentation results to frame if needed
		if ( this->GetPatRecognitionResult()->GetDotsFound() && ( this->EnableSegmentationAnalysis || this->CalibrationMode == OFFLINE) )
		{
			// Draw segmentation result to image
      this->GetPatternRecognition()->DrawResults( trackedFrame->ImageData->GetBufferPointer() );
		} 

		if( !this->GetPatRecognitionResult()->GetDotsFound() )
		{
			LOG_DEBUG("The segmentation cannot locate any meaningful targets, the image was ignored!"); 
			return PLUS_FAIL; 
		}

		// Add the segmentation result to the SegmentedFrameContainer
		SegmentedFrame segmentedFrame; 
		segmentedFrame.SegResults = this->PatRecognitionResult; 
		segmentedFrame.TrackedFrameInfo = new TrackedFrame(*this->TrackedFrameListContainer[dataType]->GetTrackedFrame(trackedFramePosition)); 
		segmentedFrame.DataType = dataType; 
		this->SegmentedFrameContainer.push_back(segmentedFrame); 

		this->ImageDataInfoContainer[dataType].NumberOfSegmentedImages++; 

		return PLUS_SUCCESS; 
	}
	catch(...)
	{
		LOG_ERROR("AddData: Failed to add tracked data!");  
		return PLUS_FAIL;
	}
}

//----------------------------------------------------------------------------
void vtkCalibrationController::ClearSegmentedFrameContainer(IMAGE_DATA_TYPE dataType)
{
  for ( SegmentedFrameList::iterator it = this->SegmentedFrameContainer.begin(); it != this->SegmentedFrameContainer.end(); )
  {
    if ( it->DataType == dataType )
    {
      if ( it->TrackedFrameInfo != NULL )
      {
        delete it->TrackedFrameInfo; 
        it->TrackedFrameInfo = NULL; 
      }
      it = this->SegmentedFrameContainer.erase(it); 
    }
    else
    {
      ++it;
    }
  } 
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::SegmentImage(vtkImageData * imageData )
{
	LOG_TRACE("vtkCalibrationController::SegmentImage - vtkImage"); 
	ImageType::Pointer frame = ImageType::New();
    if ( UsImageConverterCommon::ConvertVtkImageToItkImage(imageData, frame) != PLUS_SUCCESS )
    {
        LOG_ERROR("Failed to convert vtk image to itk image!"); 
        return PLUS_FAIL; 

    }
	return this->SegmentImage(frame);
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::SegmentImage(const ImageType::Pointer& imageData)
{
	LOG_TRACE("vtkCalibrationController::SegmentImage - itkImage"); 
	try
	{
		if ( ! this->GetInitialized() )
		{
			this->Initialize(); 
		}

		// Check frame size before segmentation 
		int frameSize[2] = {imageData->GetLargestPossibleRegion().GetSize()[0], imageData->GetLargestPossibleRegion().GetSize()[1]}; 
    if ( this->GetPatternRecognition()->GetFidSegmentation()->GetFrameSize()[0] != frameSize[0] || this->GetPatternRecognition()->GetFidSegmentation()->GetFrameSize()[1] != frameSize[1] )
		{
			LOG_ERROR("Unable to add frame to calibrator! Frame size mismatch: actual (" 
				<< frameSize[0] << "x" << frameSize[1] << ") expected (" 
				<< this->GetPatternRecognition()->GetFidSegmentation()->GetFrameSize()[0] << "x" << this->GetPatternRecognition()->GetFidSegmentation()->GetFrameSize()[1] << ")"); 
			return PLUS_FAIL; 
		}

		// Send the image to the Segmentation component for segmentation
    this->GetPatternRecognition()->RecognizePattern( imageData->GetBufferPointer(), *(this->GetPatRecognitionResult()));	
   
		return PLUS_SUCCESS;
	}
	catch(...)
	{
		LOG_ERROR("SegmentImage: The segmentation has failed for due to UNKNOWN exception thrown, the image was ignored!!!"); 
		return PLUS_FAIL; 
	}
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::SetOfflineImageData(const ImageType::Pointer& frame)
{
	LOG_TRACE("vtkCalibrationController::AddFrameToRenderer"); 
	if ( ! this->GetEnableVisualization() ) 
	{
		// We don't want to render anything
		return PLUS_SUCCESS; 
	}

  if ( frame.IsNull() )
  {
    LOG_ERROR("Failed to add frame to the renderer - frame is NULL!"); 
    return PLUS_FAIL; 
  }

  if ( this->OfflineImageData == NULL )
  {
    this->OfflineImageData = vtkImageData::New(); 
  }

  if ( UsImageConverterCommon::ConvertItkImageToVtkImage(frame, this->OfflineImageData) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to convert itk image to vtk image!"); 
    return PLUS_FAIL;
  }

  this->OfflineImageData->Modified();

  return PLUS_SUCCESS; 
}


//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::SetOfflineImageData(vtkImageData* frame)
{
	LOG_TRACE("vtkCalibrationController::AddFrameToRenderer"); 
	if ( ! this->GetEnableVisualization() ) 
	{
		// We don't want to render anything
		return PLUS_SUCCESS; 
	}

  if ( this->OfflineImageData == NULL )
  {
    this->OfflineImageData = vtkImageData::New(); 
  }

  this->OfflineImageData->DeepCopy(frame); 

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
void vtkCalibrationController::ConvertVtkMatrixToVnlMatrix(vtkMatrix4x4* inVtkMatrix, vnl_matrix<double>& outVnlMatrix )
{
	LOG_TRACE("vtkCalibrationController::ConvertVtkMatrixToVnlMatrix"); 
	for (int row = 0; row < 4; row++)
	{
		for (int column = 0; column < 4; column++)
		{
			outVnlMatrix.put(row,column, inVtkMatrix->GetElement(row,column)); 
		}
	}
}

//----------------------------------------------------------------------------
void vtkCalibrationController::ConvertVnlMatrixToVtkMatrix(vnl_matrix<double>& inVnlMatrix, vtkMatrix4x4* outVtkMatrix )
{
	LOG_TRACE("vtkCalibrationController::ConvertVnlMatrixToVtkMatrix"); 
	outVtkMatrix->Identity(); 

	for (int row = 0; row < 3; row++)
	{
		for (int column = 0; column < 4; column++)
		{
			outVtkMatrix->SetElement(row,column, inVnlMatrix.get(row, column) ); 
		}
	}
}

//----------------------------------------------------------------------------
void vtkCalibrationController::ConvertVtkMatrixToVnlMatrixInMeter(vtkMatrix4x4* inVtkMatrix, vnl_matrix<double>& outVnlMatrix )
{
	LOG_TRACE("vtkCalibrationController::ConvertVtkMatrixToVnlMatrixInMeter"); 
	ConvertVtkMatrixToVnlMatrix(inVtkMatrix, outVnlMatrix); 

	// Option: convert the translation to meters
	const double TxInM = 0.001 * outVnlMatrix.get(0,3);
	const double TyInM = 0.001 * outVnlMatrix.get(1,3);
	const double TzInM = 0.001 * outVnlMatrix.get(2,3);

	const double translationInMeters[] = {TxInM, TyInM, TzInM, 1};
	vnl_vector<double> translationInMetersVector(4);
	translationInMetersVector.set( translationInMeters );

	outVnlMatrix.set_column(3, translationInMetersVector);
}

//----------------------------------------------------------------------------
void  vtkCalibrationController::CreateTrackedFrame(const ImageType::Pointer& imageData, const double probePosition, const double probeRotation, const double templatePosition, IMAGE_DATA_TYPE dataType, TrackedFrame& trackedFrame)
{
	LOG_TRACE("vtkCalibrationController::CreateTrackedFrame - with optical readings"); 
	trackedFrame.Timestamp = 0; // TODO: current time or 0? 
	trackedFrame.DefaultFrameTransformName = "None"; 

	std::ostringstream strProbePosition;
	strProbePosition << probePosition; 
	trackedFrame.SetCustomFrameField("ProbePosition", strProbePosition.str()); 

	std::ostringstream strProbeRotation;
	strProbeRotation << probeRotation; 
	trackedFrame.SetCustomFrameField("ProbeRotation", strProbeRotation.str()); 

	std::ostringstream strTemplatePosition;
	strTemplatePosition << templatePosition; 
	trackedFrame.SetCustomFrameField("TemplatePosition", strTemplatePosition.str()); 

	trackedFrame.ImageData = imageData;
}

//----------------------------------------------------------------------------
void  vtkCalibrationController::CreateTrackedFrame(const ImageType::Pointer& imageData, vtkMatrix4x4* transform, IMAGE_DATA_TYPE dataType, TrackedFrame& trackedFrame)
{
	LOG_TRACE("vtkCalibrationController::CreateTrackedFrame - with transform"); 
	trackedFrame.Timestamp = 0; // TODO: current time or 0?
	trackedFrame.DefaultFrameTransformName = "ToolToTrackerTransform"; 

	std::ostringstream strToolToTracker; 
	double dToolToTracker[ 16 ];
	vtkMatrix4x4::DeepCopy( dToolToTracker, transform );
	for ( int i = 0; i < 16; ++ i )
	{
		strToolToTracker << dToolToTracker[ i ] << " ";
	}

	trackedFrame.SetCustomFrameField(trackedFrame.DefaultFrameTransformName, strToolToTracker.str()); 

	trackedFrame.ImageData = imageData;
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::ReadConfiguration( const char* configFileNameWithPath )
{
	LOG_TRACE("vtkCalibrationController::ReadConfiguration"); 
	this->SetConfigurationFileName(configFileNameWithPath); 

	this->SetConfigurationData(vtkXMLUtilities::ReadElementFromFile(this->GetConfigurationFileName())); 
	return this->ReadConfiguration(this->ConfigurationData);  //TODO temporary code
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::ReadConfiguration( vtkXMLDataElement* configData )
{
	LOG_TRACE("vtkCalibrationController::ReadConfiguration"); 
	if ( configData == NULL )
	{
		LOG_ERROR("Unable to read configuration"); 
		return PLUS_FAIL; 
	}

	vtkSmartPointer<vtkXMLDataElement> usCalibration = configData->FindNestedElementWithName("USCalibration");
	if (usCalibration == NULL)
  {
    LOG_ERROR("Cannot find USCalibration element in XML tree!");
    return PLUS_FAIL;
	}

  //Setting the fiducial pattern recognition
  this->PatternRecognition.ReadConfiguration(configData);

	// Tracked frame specifications
	//********************************************************************
  std::vector<vtkTrackedFrameList*>::iterator it;
  for (it = this->TrackedFrameListContainer.begin(); it != this->TrackedFrameListContainer.end(); ++it)
  {
	  if ((*it)->ReadConfiguration(configData) != PLUS_SUCCESS)
	  {
      LOG_ERROR("TrackedFrameList configuration cannot be read!");
	    return PLUS_FAIL;
	  }
  }

	// Calibration controller specifications
	//********************************************************************
	if (this->ReadPhantomDefinition(configData) != PLUS_SUCCESS)
	{
    LOG_ERROR("Unable to read phantom definition!");
  	return PLUS_FAIL;
	}

	// Calibration controller specifications
	//********************************************************************
	vtkSmartPointer<vtkXMLDataElement> calibrationController = usCalibration->FindNestedElementWithName("CalibrationController"); 
	if (this->ReadCalibrationControllerConfiguration(calibrationController) != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkCalibrationController::ReadCalibrationControllerConfiguration( vtkXMLDataElement* calibrationController )
{
	LOG_TRACE("vtkCalibrationController::ReadCalibrationControllerConfiguration"); 
	if ( calibrationController == NULL) 
	{
		LOG_WARNING("Unable to read the CalibrationController XML data element!"); 
		return PLUS_FAIL; 
	}

	// Path to output calibration results
	const char* outputPath = calibrationController->GetAttribute("OutputPath"); 
	if ( outputPath != NULL) 
	{
		std::string fullOutputPath = vtksys::SystemTools::CollapseFullPath(outputPath, this->ProgramFolderPath); 
		vtkSmartPointer<vtkDirectory> dir = vtkSmartPointer<vtkDirectory>::New(); 
		if ( dir->Open(fullOutputPath.c_str()) == 0 ) 
		{	
			dir->MakeDirectory(fullOutputPath.c_str()); 
		}
		this->SetOutputPath(fullOutputPath.c_str()); 
	}
	else
	{
		// Set to the current working directory
		this->SetOutputPath(vtksys::SystemTools::GetCurrentWorkingDirectory().c_str()); 
	}

	// Enable/disable the tracked sequence data saving to metafile
	const char* enableTrackedSequenceDataSaving = calibrationController->GetAttribute("EnableTrackedSequenceDataSaving"); 
	if ( enableTrackedSequenceDataSaving != NULL &&  STRCASECMP( "TRUE", enableTrackedSequenceDataSaving ) == 0) 
	{
		this->EnableTrackedSequenceDataSavingOn(); 
	}
	else /* FALSE */
	{
		this->EnableTrackedSequenceDataSavingOff(); 
	}

	// Enable/disable the the erroneously segmented data saving to metafile
	const char* enableErroneouslySegmentedDataSaving = calibrationController->GetAttribute("EnableErroneouslySegmentedDataSaving"); 
	if ( enableErroneouslySegmentedDataSaving != NULL &&  STRCASECMP( "TRUE", enableErroneouslySegmentedDataSaving ) == 0) 
	{
		this->EnableErroneouslySegmentedDataSavingOn(); 
	}
	else /* FALSE */
	{
		this->EnableErroneouslySegmentedDataSavingOff(); 
	}

	// To enable/disable the segmentation analysis
	const char* enableSegmentationAnalysis = calibrationController->GetAttribute("EnableSegmentationAnalysis"); 
	if ( enableSegmentationAnalysis != NULL &&  STRCASECMP( "TRUE", enableSegmentationAnalysis ) == 0 ) 
	{
		this->EnableSegmentationAnalysisOn(); 
	}
	else
	{
		this->EnableSegmentationAnalysisOff(); 
	}

	return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------

PlusStatus vtkCalibrationController::ReadPhantomDefinition(vtkXMLDataElement* config)
{
	LOG_TRACE("vtkCalibrationController::ReadPhantomDefinition");

	if ( config == NULL )
	{
		LOG_ERROR("Configuration XML data element is NULL"); 
		return PLUS_FAIL;
	}

	vtkSmartPointer<vtkXMLDataElement> phantomDefinition = config->FindNestedElementWithName("PhantomDefinition");
	if (phantomDefinition == NULL)
  {
		LOG_ERROR("No phantom definition is found in the XML tree!");
		return PLUS_FAIL;
	}
  else
	{
		std::vector<NWire> tempNWires;

		// Load model information
		vtkSmartPointer<vtkXMLDataElement> model = phantomDefinition->FindNestedElementWithName("Model"); 
		if (model == NULL) {
			LOG_WARNING("Phantom model information not found - no model displayed");
		} else {
			const char* file = model->GetAttribute("File");
			if (file) {
				if ((strstr(file, ".stl") != NULL) || ((strstr(file, ".STL") != NULL))) { // If filename contains ".stl" or ".STL" then it is valid, else we do not search for it (and do not return with warning either, because some time we just do not fill that field because we do not have the file)
					std::string searchResult;
					if (STRCASECMP(vtkConfigurationTools::GetInstance()->GetConfigurationDirectory(), "") == 0) {
						std::string configurationDirectory = vtksys::SystemTools::GetFilenamePath(this->ConfigurationFileName);
						searchResult = vtkConfigurationTools::GetFirstFileFoundInParentOfDirectory(file, configurationDirectory.c_str());
					} else {
						searchResult = vtkConfigurationTools::GetFirstFileFoundInConfigurationDirectory(file);
					}
					if (STRCASECMP("", searchResult.c_str()) != 0) {
						this->SetPhantomModelFileName(searchResult.c_str());
					}
				} else {
					LOG_INFO("'" << file << "' does not appear to be a valid phantom model file name, so it was not searched for");
				}
			}

			// ModelToPhantomTransform - Transforming input model for proper visualization
			double* modelToPhantomTransformVector = new double[16]; 
			if (model->GetVectorAttribute("ModelToPhantomTransform", 16, modelToPhantomTransformVector)) {
				vtkSmartPointer<vtkMatrix4x4> modelToPhantomTransformMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
				modelToPhantomTransformMatrix->Identity();
				modelToPhantomTransformMatrix->DeepCopy(modelToPhantomTransformVector);

				vtkSmartPointer<vtkTransform> modelToPhantomTransform = vtkSmartPointer<vtkTransform>::New();
				modelToPhantomTransform->SetMatrix(modelToPhantomTransformMatrix);
				this->SetModelToPhantomTransform(modelToPhantomTransform);
			}
			delete[] modelToPhantomTransformVector;
		}

		// Load geometry
		vtkSmartPointer<vtkXMLDataElement> geometry = phantomDefinition->FindNestedElementWithName("Geometry"); 
		if (geometry == NULL) {
			LOG_ERROR("Phantom geometry information not found!");
			return PLUS_FAIL;
		} else {

			tempNWires.clear();

			// Finding of NWires and extracting the endpoints
			int numberOfGeometryChildren = geometry->GetNumberOfNestedElements();
			for (int i=0; i<numberOfGeometryChildren; ++i) {
				vtkSmartPointer<vtkXMLDataElement> nWireElement = geometry->GetNestedElement(i);

				if ((nWireElement == NULL) || (STRCASECMP("NWire", nWireElement->GetName()))) {
					continue;
				}

				NWire nWire;

				int numberOfWires = nWireElement->GetNumberOfNestedElements();

				if (numberOfWires != 3) {
					LOG_WARNING("NWire contains unexpected number of wires - skipped");
					continue;
				}

				for (int j=0; j<numberOfWires; ++j) {
					vtkSmartPointer<vtkXMLDataElement> wireElement = nWireElement->GetNestedElement(j);

					if (wireElement == NULL) {
						LOG_WARNING("Invalid Wire description in NWire - skipped");
						break;
					}

					Wire wire;

					int wireId = -1;
					if ( wireElement->GetScalarAttribute("Id", wireId) ) 
					{
						wire.id = wireId; 
					}
					else
					{
						LOG_WARNING("Wire id not found - skipped");
						continue;
					}

					const char* wireName =  wireElement->GetAttribute("Name"); 
					if ( wireName != NULL )
					{
						wire.name = wireName;
					}
					if (! wireElement->GetVectorAttribute("EndPointFront", 3, wire.endPointFront)) {
						LOG_WARNING("Wrong wire end point detected - skipped");
						continue;
					}
					if (! wireElement->GetVectorAttribute("EndPointBack", 3, wire.endPointBack)) {
						LOG_WARNING("Wrong wire end point detected - skipped");
						continue;
					}

					nWire.wires[j] = wire;
				}

				tempNWires.push_back(nWire);
			}
		}

    this->GetPatternRecognition()->GetFidSegmentation()->SetNWires(tempNWires);
    this->GetPatternRecognition()->GetFidLineFinder()->SetNWires(tempNWires);
    this->GetPatternRecognition()->GetFidLabelling()->SetNWires(tempNWires);
	}

  this->GetPatternRecognition()->ReadConfiguration(config);

	//TODO Load registration?

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
