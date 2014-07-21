/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#define USE_vtkPolyDataToOrientedImageStencil

#include "PlusConfigure.h"
#include "vtkUsSimulatorAlgo.h"
#include "vtkImageAlgorithm.h"
#include "vtkInformation.h"
#include "vtkTransform.h"
#include "vtkPolyData.h"
#include "vtkPolyDataToImageStencil.h"
#include "vtkSmartPointer.h"
#include "vtkImageStencil.h"
#include "vtkPolyDataNormals.h"
#include "vtkTriangleFilter.h"
#include "vtkStripper.h"
#include "vtkInformationVector.h"
#include "vtkImageData.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkImageStencilData.h"
#include "vtkPolyData.h"
#include "vtkSTLReader.h"
#include "vtkXMLPolyDataReader.h"
#include "vtksys/SystemTools.hxx"

#ifdef USE_vtkPolyDataToOrientedImageStencil
#include "vtkPolyDataToOrientedImageStencil.h"
#endif

//-----------------------------------------------------------------------------

vtkCxxRevisionMacro(vtkUsSimulatorAlgo, "$Revision: 1.0 $");
vtkStandardNewMacro(vtkUsSimulatorAlgo);

//-----------------------------------------------------------------------------
vtkUsSimulatorAlgo::vtkUsSimulatorAlgo()
{
  SetNumberOfInputPorts(1);
  SetNumberOfOutputPorts(1);

  this->BackgroundValue = 155;
  this->StencilBackgroundImage = NULL;
  this->ModelToImageMatrix = NULL;

  this->FrameSize[0] = 640;
  this->FrameSize[1] = 480;

  this->SpacingMmPerPixel[0] = this->SpacingMmPerPixel[1] = 1.0;

  this->ModelFileName = NULL;
  this->ImageCoordinateFrame = NULL;
  this->ReferenceCoordinateFrame = NULL;
}

//-----------------------------------------------------------------------------
vtkUsSimulatorAlgo::~vtkUsSimulatorAlgo()
{
  SetModelToImageMatrix(NULL); 
  SetStencilBackgroundImage(NULL);
  SetModelFileName(NULL); 
  SetImageCoordinateFrame(NULL); 
  SetReferenceCoordinateFrame(NULL); 
}

//-----------------------------------------------------------------------------
void vtkUsSimulatorAlgo::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
}

//-----------------------------------------------------------------------------
int vtkUsSimulatorAlgo::FillInputPortInformation(int, vtkInformation * info)
{
  info->Set(vtkImageAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData"); 
  return 1; 
}

//-----------------------------------------------------------------------------
int vtkUsSimulatorAlgo::FillOutputPortInformation(int, vtkInformation * info)
{
  info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkImageData"); 
  return 1; 
}

//-----------------------------------------------------------------------------
int vtkUsSimulatorAlgo::RequestData(vtkInformation* request,vtkInformationVector** inputVector,vtkInformationVector* outputVector)
{
  if (this->ModelFileName == NULL)
    {
	    LOG_ERROR("ModelFileName is not specified in vtkUsSimulatorAlgo element of the configuration nor is set as input!");
      return PLUS_FAIL;     
    }
  
  if (this->ModelToImageMatrix == NULL)
  {
    LOG_ERROR(" No Model to US image transform specified " ); 
    return 1; 
  }
  if (this->StencilBackgroundImage == NULL)
  {
    LOG_ERROR(" background image necessary for stencil creation not specified " ); 
    return 1; 
  } 

  // Get input
  vtkInformation* inInfoPort = inputVector[0]->GetInformationObject(0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0); 

  vtkSmartPointer<vtkPolyData> modelModel = vtkPolyData::SafeDownCast(inInfoPort->Get(vtkDataObject::DATA_OBJECT()));
  if (modelModel == NULL)
  {
    LOG_ERROR("Model specified is empty");
    return 1; 
  }

  vtkSmartPointer<vtkImageData> simulatedUsImage = vtkImageData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT())); 
  if (simulatedUsImage == NULL)
  {
    LOG_ERROR("vtkUsSimulatorAlgo output type is invalid");
    return 1; 
  }

  vtkSmartPointer<vtkPolyDataNormals> normalFilter=vtkSmartPointer<vtkPolyDataNormals>::New();
  normalFilter->SetInput(modelModel);
  normalFilter->ConsistencyOn();

  // Make sure that we have a clean triangle polydata
  vtkSmartPointer<vtkTriangleFilter> triangle=vtkSmartPointer<vtkTriangleFilter>::New();
  triangle->SetInputConnection(normalFilter->GetOutputPort());

  // Convert to triangle strip
  vtkSmartPointer<vtkStripper> stripper=vtkSmartPointer<vtkStripper>::New();
  stripper->SetInputConnection(triangle->GetOutputPort());

#ifdef USE_vtkPolyDataToOrientedImageStencil

  // Create PolyData to Image stencil
  vtkSmartPointer<vtkPolyDataToOrientedImageStencil> modelStencil = vtkSmartPointer<vtkPolyDataToOrientedImageStencil>::New();
  modelStencil->SetInputConnection(stripper->GetOutputPort());
  // volumevoxel origin=0 and orientation=identity in the vtkImageData => VolumeVoxel = Model
  // OrientedVolumeVoxel = Image
  // ===> ModelToImage = VolumeVoxelToOrientedVolumeVoxel
  modelStencil->SetVolumeVoxelToOrientedVolumeVoxel(this->ModelToImageMatrix);  

#else

  // Transform model points from the MODEL coordinate system to image coordinate system
  vtkSmartPointer<vtkTransform> modelToImageTransform = vtkSmartPointer<vtkTransform>::New(); 
  modelToImageTransform->SetMatrix(this->ModelToImageMatrix);   
  vtkSmartPointer<vtkTransformPolyDataFilter> transformModelFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  transformModelFilter->SetInputConnection(stripper->GetOutputPort());
  transformModelFilter->SetTransform(modelToImageTransform);
  transformModelFilter->Update();
  // Model points in the image coordinate system
  vtkPolyData* modelImage = transformModelFilter->GetOutput();
  // Create PolyData to Image stencil
  vtkSmartPointer<vtkPolyDataToImageStencil> modelStencil = vtkSmartPointer<vtkPolyDataToImageStencil>::New();
  modelStencil->SetInput(modelImage); 

#endif    

  modelStencil->SetOutputSpacing(this->StencilBackgroundImage->GetSpacing()); 
  modelStencil->SetOutputOrigin(this->StencilBackgroundImage->GetOrigin());
  modelStencil->SetOutputWholeExtent(this->StencilBackgroundImage->GetExtent()); 
  modelStencil->Update(); 

  // Create Image stencil
  vtkSmartPointer<vtkImageStencil> combineModelwithBackgroundStencil = vtkSmartPointer<vtkImageStencil>::New();
  combineModelwithBackgroundStencil->SetInput(this->StencilBackgroundImage);
  combineModelwithBackgroundStencil->SetStencil(modelStencil->GetOutput());
  combineModelwithBackgroundStencil->ReverseStencilOff();
  combineModelwithBackgroundStencil->SetBackgroundValue(this->BackgroundValue);
  combineModelwithBackgroundStencil->Update();
  vtkImageData *combinedStencilOutput = combineModelwithBackgroundStencil->GetOutput();

  simulatedUsImage->DeepCopy(combinedStencilOutput); 

  
   

  return 1; 
}

//-----------------------------------------------------------------------------
PlusStatus vtkUsSimulatorAlgo::ReadConfiguration(vtkXMLDataElement* config)
{
  LOG_TRACE("vtkUsSimulatorVideoSource::ReadConfiguration"); 
  if ( config == NULL )
  {
    LOG_ERROR("Unable to configure Saved Data video source! (XML data element is NULL)"); 
    return PLUS_FAIL; 
  }

  // vtkUsSimulatorAlgo section
  vtkXMLDataElement* usSimulatorAlgoElement = config->FindNestedElementWithName("vtkUsSimulatorAlgo"); 

  if (usSimulatorAlgoElement == NULL)
  {
    LOG_ERROR("Unable to find vtkUsSimulatorAlgo element in XML tree!"); 
    return PLUS_FAIL;     
  }

  // Background value
  int backgroundValue = -1;
  if ( usSimulatorAlgoElement->GetScalarAttribute("BackgroundValue", backgroundValue)) 
  {
    this->SetBackgroundValue(backgroundValue); 
  }

  // Frame size
	int frameSize[2] = {0}; 
	if ( usSimulatorAlgoElement->GetVectorAttribute("FrameSize", 2, frameSize) )
	{
    this->FrameSize[0] = frameSize[0];
    this->FrameSize[1] = frameSize[1];
	}
  else
  {
		LOG_WARNING("Cannot find FrameSize attribute in the configuration");
	}

  // Pixel spacing
	double spacingMmPerPixel[2] = {0};
	if ( usSimulatorAlgoElement->GetVectorAttribute("SpacingMmPerPixel", 2, spacingMmPerPixel) )
	{
    this->SpacingMmPerPixel[0] = spacingMmPerPixel[0];
    this->SpacingMmPerPixel[1] = spacingMmPerPixel[1];
	}
  else
  {
		LOG_ERROR("Cannot find SpacingMmPerPixel attribute in the configuration");
    return PLUS_FAIL;     
	}

  // Model file name
  
    const char* modelFileName = usSimulatorAlgoElement->GetAttribute("ModelFileName");

    if(modelFileName)
    {
 
      std::string foundAbsoluteImagePath;
      if (vtkPlusConfig::GetAbsoluteImagePath(modelFileName, foundAbsoluteImagePath) == PLUS_SUCCESS)
      {
        LoadModel(foundAbsoluteImagePath);  
      }
    
      else
      {
        LOG_WARNING("Cannot find input model file!");
     // return PLUS_FAIL; comment out, because should fail later when update is called. 
      }
    }
    else
    {
      LOG_WARNING("Cannot find input model file!"); 
    }

  // Reference coordinate frame
  const char* imageCoordinateFrame = usSimulatorAlgoElement->GetAttribute("ImageCoordinateFrame");
  if (imageCoordinateFrame == NULL)
  {
	  LOG_ERROR("ImageCoordinateFrame is not specified in vtkUsSimulatorAlgo element of the configuration!");
    return PLUS_FAIL;     
  }
  this->SetImageCoordinateFrame(imageCoordinateFrame);

  // Reference coordinate frame
  const char* referenceCoordinateFrame = usSimulatorAlgoElement->GetAttribute("ReferenceCoordinateFrame");
  if (referenceCoordinateFrame == NULL)
  {
	  LOG_ERROR("ReferenceCoordinateFrame is not specified in vtkUsSimulatorAlgo element of the configuration!");
    return PLUS_FAIL;     
  }
  this->SetReferenceCoordinateFrame(referenceCoordinateFrame);

  return PLUS_SUCCESS;
}
//-----------------------------------------------------------------------------

PlusStatus vtkUsSimulatorAlgo::LoadModel(std::string absoluteImagePath)
{
      this->SetModelFileName(absoluteImagePath.c_str());
        // Load Model
  
      std::string fileExt=vtksys::SystemTools::GetFilenameLastExtension(this->ModelFileName);
      vtkSmartPointer<vtkPolyData> model = vtkSmartPointer<vtkPolyData>::New(); 
 
  
  
      if (STRCASECMP(fileExt.c_str(),".stl")==0)
      {  
        vtkSmartPointer<vtkSTLReader> modelReader = vtkSmartPointer<vtkSTLReader>::New();
        modelReader->SetFileName(this->ModelFileName);
        modelReader->Update();
        model = modelReader->GetOutput();
      }
  
      else //if (STRCASECMP(fileExt.c_str(),".vtp")==0)
      {
        vtkSmartPointer<vtkXMLPolyDataReader> modelReader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
        modelReader->SetFileName(this->ModelFileName);
        modelReader->Update();
        model = modelReader->GetOutput();
      }
    
      if (model == NULL)
      {
        LOG_ERROR("Model specified cannot be found, check path");
        return PLUS_FAIL;
      }
   
    this->SetInput(model);
    return PLUS_SUCCESS;
}



PlusStatus vtkUsSimulatorAlgo::CreateStencilBackgroundImage()
{
  vtkSmartPointer<vtkImageData> stencilBackgroundImage = vtkSmartPointer<vtkImageData>::New(); 
  stencilBackgroundImage->SetSpacing(this->SpacingMmPerPixel[0],this->SpacingMmPerPixel[1],1);
  stencilBackgroundImage->SetOrigin(0,0,0);

  //int* frameSize = frame->GetFrameSize();
  stencilBackgroundImage->SetExtent(0, this->FrameSize[0]-1, 0, this->FrameSize[1]-1, 0, 0);

  stencilBackgroundImage->SetScalarTypeToUnsignedChar();
  stencilBackgroundImage->SetNumberOfScalarComponents(1);
  stencilBackgroundImage->AllocateScalars(); 

  int* extent = stencilBackgroundImage->GetExtent();
  memset(stencilBackgroundImage->GetScalarPointer(), 0,
    ((extent[1]-extent[0]+1)*(extent[3]-extent[2]+1)*(extent[5]-extent[4]+1)*stencilBackgroundImage->GetScalarSize()*stencilBackgroundImage->GetNumberOfScalarComponents()));

  this->SetStencilBackgroundImage(stencilBackgroundImage);

  return PLUS_SUCCESS;
}