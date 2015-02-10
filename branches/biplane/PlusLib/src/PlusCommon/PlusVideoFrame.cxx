/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "PlusVideoFrame.h"
#include "itkImageBase.h"
#include "vtkBMPReader.h"
#include "vtkImageData.h"
#include "vtkImageReader.h"
#include "vtkObjectFactory.h"
#include "vtkPNMReader.h"
#include "vtkTIFFReader.h"

#ifdef PLUS_USE_OpenIGTLink
#include "igtlImageMessage.h"
#endif

//----------------------------------------------------------------------------

namespace
{
  //----------------------------------------------------------------------------
  template<class ScalarType>
  PlusStatus FlipImageGeneric(void* inBuff, int numberOfScalarComponents, int width, int height, int depth, const PlusVideoFrame::FlipInfoType& flipInfo, void* outBuff)
  {
    // TODO : determine how to do flipz
    if (flipInfo.doubleRow)
    {
      if (height%2 != 0)
      {
        LOG_ERROR("Cannot flip image with pairs of rows kept together, as number of rows is odd ("<<height<<")");
        return PLUS_FAIL;
      }
      width*=2;
      height/=2;
    }
    if (flipInfo.doubleColumn)
    {
      if (width%2 != 0)
      {
        LOG_ERROR("Cannot flip image with pairs of columns kept together, as number of columns is odd ("<<width<<")");
        return PLUS_FAIL;
      }
    }

    if (!flipInfo.hFlip && flipInfo.vFlip && !flipInfo.eFlip)
    {
      // flip Y    
      ScalarType* inputPixel = (ScalarType*)inBuff;
      // Set the target position pointer to the first pixel of the last row
      ScalarType* outputPixel = (ScalarType*)outBuff + width*numberOfScalarComponents*(height-1);
      // Copy the image row-by-row, reversing the row order
      for (int y=height; y>0; y--)
      {
        memcpy(outputPixel, inputPixel, width * sizeof(ScalarType) * numberOfScalarComponents);
        inputPixel += (width * numberOfScalarComponents);
        outputPixel -= (width * numberOfScalarComponents);
      }
    }
    else if (flipInfo.hFlip && !flipInfo.vFlip && !flipInfo.eFlip)
    {
      // flip X    
      if (flipInfo.doubleColumn)
      {
        ScalarType* inputPixel = (ScalarType*)inBuff;
        // Set the target position pointer to the last pixel of the first row
        ScalarType* outputPixel = (ScalarType*)outBuff + width*numberOfScalarComponents - 2*numberOfScalarComponents;
        // Copy the image row-by-row, reversing the pixel order in each row
        for (int y = height; y > 0; y--)
        {
          for (int x = width/2; x > 0; x--)
          {
            // For each scalar, copy it
            for( int s = 0; s < numberOfScalarComponents; ++s)
            {
              *(outputPixel - 1*numberOfScalarComponents + s) = *(inputPixel + s);
              *(outputPixel + s) = *(inputPixel + 1*numberOfScalarComponents + s);
            }
            inputPixel += 2*numberOfScalarComponents;
            outputPixel -= 2*numberOfScalarComponents;
          }
          outputPixel += 2*width*numberOfScalarComponents;
        }
      }
      else
      {
        ScalarType* inputPixel = (ScalarType*)inBuff;
        // Set the target position pointer to the last pixel of the first row
        ScalarType* outputPixel = (ScalarType*)outBuff + width*numberOfScalarComponents - 1*numberOfScalarComponents;
        // Copy the image row-by-row, reversing the pixel order in each row
        for (int y = height; y > 0; y--)
        {
          for (int x = width; x > 0; x--)
          {
            // For each scalar, copy it
            for( int s = 0; s < numberOfScalarComponents; ++s)
            {
              *(outputPixel+s) = *(inputPixel+s);
            }
            inputPixel += numberOfScalarComponents;
            outputPixel -= numberOfScalarComponents;
          }
          outputPixel += 2*width*numberOfScalarComponents;
        }
      }
    }
    else if (flipInfo.hFlip && flipInfo.vFlip && !flipInfo.eFlip)
    {
      // flip X and Y
      if (flipInfo.doubleColumn)
      {
        ScalarType* inputPixel = (ScalarType*)inBuff;
        // Set the target position pointer to the last pixel
        ScalarType* outputPixel = (ScalarType*)outBuff + height*width*numberOfScalarComponents - 1*numberOfScalarComponents;
        // Copy the image pixelpair-by-pixelpair, reversing the pixel order
        for (int p = width*height/2; p > 0; p--)
        {
          // For each scalar, copy it
          for( int s = 0; s < numberOfScalarComponents; ++s)
          {
            *(outputPixel - 1*numberOfScalarComponents + s) = *(inputPixel + s);
            *(outputPixel + s) = *(inputPixel + 1*numberOfScalarComponents + s);
          }
          inputPixel += 2*numberOfScalarComponents;
          outputPixel -= 2*numberOfScalarComponents;
        }
      }
      else
      {
        ScalarType* inputPixel = (ScalarType*)inBuff;
        // Set the target position pointer to the last pixel
        ScalarType* outputPixel = (ScalarType*)outBuff + height*width*numberOfScalarComponents - 1*numberOfScalarComponents;
        // Copy the image pixel-by-pixel, reversing the pixel order
        for (int p = width*height; p > 0; p--)
        {
          // For each scalar, copy it
          for( int s = 0; s < numberOfScalarComponents; ++s)
          {
            *(outputPixel+s) = *(inputPixel+s);
          }
          inputPixel += numberOfScalarComponents;
          outputPixel -= numberOfScalarComponents;
        }
      }
    }
    else
    {
      // TODO : implement slice reordering
      LOG_ERROR("Reorienting images along Z direction not implemented.");
    }

    return PLUS_SUCCESS;
  }
}

//----------------------------------------------------------------------------
PlusVideoFrame::PlusVideoFrame()
: Image(NULL)
, ImageType(US_IMG_BRIGHTNESS)
, ImageOrientation(US_IMG_ORIENT_MF)
{
}

//----------------------------------------------------------------------------
PlusVideoFrame::PlusVideoFrame(const PlusVideoFrame &videoItem)
: Image(NULL)
, ImageType(US_IMG_BRIGHTNESS)
, ImageOrientation(US_IMG_ORIENT_MF)
{
  *this = videoItem;
}

//----------------------------------------------------------------------------
PlusVideoFrame::~PlusVideoFrame()
{
  DELETE_IF_NOT_NULL(this->Image);
}

//----------------------------------------------------------------------------
PlusVideoFrame& PlusVideoFrame::operator=(PlusVideoFrame const&videoItem)
{
  // Handle self-assignment
  if (this == &videoItem)
  {
    return *this;
  }

  this->ImageType = videoItem.ImageType;
  this->ImageOrientation = videoItem.ImageOrientation;

  // Copy the pixels. Don't use image duplicator, because that wouldn't reuse the existing buffer
  if ( videoItem.GetFrameSizeInBytes() > 0)
  {
    int frameSize[3] = {0,0,0};
    videoItem.GetFrameSize(frameSize);

    if ( this->AllocateFrame(frameSize, videoItem.GetVTKScalarPixelType(), videoItem.GetNumberOfScalarComponents()) != PLUS_SUCCESS )
    {
      LOG_ERROR("Failed to allocate memory for the new frame in the buffer!"); 
    }
    else
    {
      memcpy(this->GetScalarPointer(), videoItem.GetScalarPointer(), this->GetFrameSizeInBytes() ); 
    }
  }

  return *this;
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::DeepCopy(PlusVideoFrame* videoItem)
{
  if ( videoItem == NULL )
  {
    LOG_ERROR("Failed to deep copy video buffer item - buffer item NULL!"); 
    return PLUS_FAIL; 
  }

  (*this)=(*videoItem);

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::FillBlank()
{
  if ( !this->IsImageValid() )
  {
    LOG_ERROR("Unable to fill image to blank, image data is NULL."); 
    return PLUS_FAIL; 
  }

  memset( this->GetScalarPointer(), 0, this->GetFrameSizeInBytes() ); 

  return PLUS_SUCCESS; 
}


//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::AllocateFrame(vtkImageData* image, const int imageSize[3], PlusCommon::VTKScalarPixelType pixType, int numberOfScalarComponents)
{  
  if ( image != NULL )
  {
    int imageExtents[6] = {0,0,0,0,0,0};
    image->GetExtent(imageExtents);
    if (imageSize[0] == imageExtents[1]-imageExtents[0] &&
      imageSize[1] == imageExtents[3]-imageExtents[2] &&
      imageSize[2] == imageExtents[5]-imageExtents[4] &&
      image->GetScalarType() == pixType &&
      image->GetNumberOfScalarComponents() == numberOfScalarComponents)
    {
      // already allocated, no change
      return PLUS_SUCCESS;
    }        
  }

  image->SetExtent(0, imageSize[0]-1, 0, imageSize[1]-1, 0, imageSize[2]-1);

#if (VTK_MAJOR_VERSION < 6)
  image->SetScalarType(pixType);
  image->SetNumberOfScalarComponents(numberOfScalarComponents);
  image->AllocateScalars();
#else
  image->AllocateScalars(pixType, numberOfScalarComponents);
#endif

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::AllocateFrame(const int imageSize[3], PlusCommon::VTKScalarPixelType pixType, int numberOfScalarComponents)
{
  if( this->GetImage() == NULL )
  {
    this->SetImageData(vtkImageData::New());
  }
  PlusStatus allocStatus = PlusVideoFrame::AllocateFrame(this->GetImage(), imageSize, pixType, numberOfScalarComponents);
  return allocStatus;
}

//----------------------------------------------------------------------------
unsigned long PlusVideoFrame::GetFrameSizeInBytes() const
{
  if (!this->IsImageValid())
  {
    return 0;
  }
  int frameSize[3] = {0,0,0};
  this->GetFrameSize(frameSize);

  if( frameSize[0] <= 0 || frameSize[1] <= 0 || frameSize[2] <= 0 )
  {
    return 0;
  }

  int bytesPerScalar = GetNumberOfBytesPerScalar(); 
  if (bytesPerScalar != 1 && bytesPerScalar != 2 && bytesPerScalar != 4)
  {
    LOG_ERROR("Unsupported scalar size: " << bytesPerScalar << " bytes/scalar component");
  }
  unsigned long frameSizeInBytes = frameSize[0] * frameSize[1] * frameSize[2] * bytesPerScalar * this->GetNumberOfScalarComponents();
  return frameSizeInBytes; 
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::DeepCopyFrom(vtkImageData* frame)
{
  if ( frame == NULL )
  {
    LOG_ERROR("Failed to deep copy from vtk image data - input frame is NULL!"); 
    return PLUS_FAIL; 
  }

  int* frameExtent = frame->GetExtent(); 
  int frameSize[3] = {( frameExtent[1] - frameExtent[0] + 1 ), ( frameExtent[3] - frameExtent[2] + 1 ), ( frameExtent[5] - frameExtent[4] + 1 ) }; 

  if ( this->AllocateFrame(frameSize, frame->GetScalarType(), frame->GetNumberOfScalarComponents()) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to allocate memory for plus video frame!"); 
    return PLUS_FAIL; 
  }

  memcpy(this->Image->GetScalarPointer(), frame->GetScalarPointer(), this->GetFrameSizeInBytes() );

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
int PlusVideoFrame::GetNumberOfBytesPerScalar() const
{
  return PlusVideoFrame::GetNumberOfBytesPerScalar(GetVTKScalarPixelType());
}

//----------------------------------------------------------------------------
int PlusVideoFrame::GetNumberOfBytesPerPixel() const
{
  return this->GetNumberOfBytesPerScalar()*this->GetNumberOfScalarComponents();
}

//----------------------------------------------------------------------------
US_IMAGE_ORIENTATION PlusVideoFrame::GetImageOrientation() const
{
  return this->ImageOrientation;
}

//----------------------------------------------------------------------------
void PlusVideoFrame::SetImageOrientation(US_IMAGE_ORIENTATION imgOrientation)
{
  this->ImageOrientation=imgOrientation;
}

//----------------------------------------------------------------------------
int PlusVideoFrame::GetNumberOfScalarComponents() const
{
  if( IsImageValid() )
  {
    return this->Image->GetNumberOfScalarComponents();
  }

  return -1;
}

//----------------------------------------------------------------------------
US_IMAGE_TYPE PlusVideoFrame::GetImageType() const
{
  return this->ImageType;
}

//----------------------------------------------------------------------------
void PlusVideoFrame::SetImageType(US_IMAGE_TYPE imgType)
{
  this->ImageType=imgType;
}

//----------------------------------------------------------------------------
void* PlusVideoFrame::GetScalarPointer() const
{
  if (!this->IsImageValid())
  {
    LOG_ERROR("Cannot get buffer pointer, the buffer hasn't been created yet");
    return NULL;
  }

  return this->Image->GetScalarPointer();
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::GetFrameSize(int frameSize[3]) const
{
  if ( !this->IsImageValid() )
  {
    frameSize[0] = frameSize[1] = frameSize[2] = 0;
    return PLUS_FAIL;
  }

  int extents[6] = {0,0,0,0,0,0};
  this->Image->GetExtent(extents);
  frameSize[0] = extents[1] - extents[0] + 1; 
  frameSize[1] = extents[3] - extents[2] + 1;
  frameSize[2] = extents[5] - extents[4] + 1;

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusCommon::VTKScalarPixelType PlusVideoFrame::GetVTKScalarPixelType() const
{
  if( this->Image == NULL || !this->IsImageValid() )
  {
    return VTK_VOID;
  }
  return this->Image->GetScalarType();
}

//----------------------------------------------------------------------------
US_IMAGE_ORIENTATION PlusVideoFrame::GetUsImageOrientationFromString( const char* imgOrientationStr )
{
  US_IMAGE_ORIENTATION imgOrientation = US_IMG_ORIENT_XX; 
  if ( imgOrientationStr == NULL )
  {
    return imgOrientation; 
  }
  else if ( STRCASECMP(imgOrientationStr, "UF" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_UF; 
  }
  else if ( STRCASECMP(imgOrientationStr, "UN" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_UN; 
  }
  else if ( STRCASECMP(imgOrientationStr, "MF" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_MF; 
  }
  else if ( STRCASECMP(imgOrientationStr, "MN" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_MN; 
  }
  else if ( STRCASECMP(imgOrientationStr, "UFA" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_UFA; 
  }
  else if ( STRCASECMP(imgOrientationStr, "UNA" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_UNA; 
  }
  else if ( STRCASECMP(imgOrientationStr, "MFA" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_MFA; 
  }
  else if ( STRCASECMP(imgOrientationStr, "MNA" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_MNA; 
  }
  else if ( STRCASECMP(imgOrientationStr, "UFD" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_UFD; 
  }
  else if ( STRCASECMP(imgOrientationStr, "UND" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_UND; 
  }
  else if ( STRCASECMP(imgOrientationStr, "MFD" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_MFD; 
  }
  else if ( STRCASECMP(imgOrientationStr, "MND" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_MND; 
  }
  else if ( STRCASECMP(imgOrientationStr, "FU" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_FU; 
  }
  else if ( STRCASECMP(imgOrientationStr, "NU" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_NU; 
  }
  else if ( STRCASECMP(imgOrientationStr, "FM" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_FM; 
  }
  else if ( STRCASECMP(imgOrientationStr, "NM" ) == 0 )
  {
    imgOrientation = US_IMG_ORIENT_NM; 
  }

  return imgOrientation; 
}

//----------------------------------------------------------------------------
const char* PlusVideoFrame::GetStringFromUsImageOrientation(US_IMAGE_ORIENTATION imgOrientation)
{
  switch (imgOrientation)
  {
  case US_IMG_ORIENT_MF: return "MF";
  case US_IMG_ORIENT_MN: return "MN";
  case US_IMG_ORIENT_UF: return "UF";
  case US_IMG_ORIENT_UN: return "UN";
  case US_IMG_ORIENT_FM: return "FM";
  case US_IMG_ORIENT_NM: return "NM";
  case US_IMG_ORIENT_FU: return "FU";
  case US_IMG_ORIENT_NU: return "NU";
  default:
    return "XX";
  }
}

//----------------------------------------------------------------------------
US_IMAGE_TYPE PlusVideoFrame::GetUsImageTypeFromString( const char* imgTypeStr )
{
  US_IMAGE_TYPE imgType = US_IMG_TYPE_XX; 
  if ( imgTypeStr == NULL )
  {
    return imgType; 
  }
  else if ( STRCASECMP(imgTypeStr, "BRIGHTNESS" ) == 0 )
  {
    imgType = US_IMG_BRIGHTNESS; 
  }
  else if ( STRCASECMP(imgTypeStr, "RF_REAL" ) == 0 )
  {
    imgType = US_IMG_RF_REAL; 
  }
  else if ( STRCASECMP(imgTypeStr, "RF_IQ_LINE" ) == 0 )
  {
    imgType = US_IMG_RF_IQ_LINE; 
  }
  else if ( STRCASECMP(imgTypeStr, "RF_I_LINE_Q_LINE" ) == 0 )
  {
    imgType = US_IMG_RF_I_LINE_Q_LINE; 
  }
  else if ( STRCASECMP(imgTypeStr, "RGB_COLOR" ) == 0 )
  {
    imgType = US_IMG_RGB_COLOR; 
  }
  return imgType; 
}

//----------------------------------------------------------------------------
const char* PlusVideoFrame::GetStringFromUsImageType(US_IMAGE_TYPE imgType)
{
  switch (imgType)
  {
  case US_IMG_BRIGHTNESS: return "BRIGHTNESS";
  case US_IMG_RF_REAL: return "RF_REAL";
  case US_IMG_RF_IQ_LINE: return "RF_IQ_LINE";
  case US_IMG_RF_I_LINE_Q_LINE: return "RF_I_LINE_Q_LINE";
  case US_IMG_RGB_COLOR: return "RGB_COLOR";
  default:
    return "XX";
  }
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::GetFlipAxes(US_IMAGE_ORIENTATION usImageOrientation1, US_IMAGE_TYPE usImageType1, US_IMAGE_ORIENTATION usImageOrientation2, FlipInfoType& flipInfo)
{  
  flipInfo.doubleRow=false;
  flipInfo.doubleColumn=false;
  if (usImageType1==US_IMG_RF_I_LINE_Q_LINE)
  {
    if ( usImageOrientation1==US_IMG_ORIENT_FM
      || usImageOrientation1==US_IMG_ORIENT_FU
      || usImageOrientation1==US_IMG_ORIENT_NM
      || usImageOrientation1==US_IMG_ORIENT_NU )
    {
      // keep line pairs together
      flipInfo.doubleRow=true;
    }
    else
    {
      LOG_ERROR("RF scanlines are expected to be in image rows");
      return PLUS_FAIL;
    }
  }
  else if (usImageType1==US_IMG_RF_IQ_LINE)
  {
    if ( usImageOrientation1==US_IMG_ORIENT_FM
      || usImageOrientation1==US_IMG_ORIENT_FU
      || usImageOrientation1==US_IMG_ORIENT_NM
      || usImageOrientation1==US_IMG_ORIENT_NU )
    {
      // keep IQ value pairs together
      flipInfo.doubleColumn=true;
    }
    else
    {
      LOG_ERROR("RF scanlines are expected to be in image rows");
      return PLUS_FAIL;
    }
  }

  flipInfo.hFlip=false; // horizontal
  flipInfo.vFlip=false; // vertical
  flipInfo.eFlip=false; // elevational
  if ( usImageOrientation1 == US_IMG_ORIENT_XX ) 
  {
    LOG_ERROR("Failed to determine the necessary image flip - unknown input image orientation 1"); 
    return PLUS_FAIL; 
  }

  if ( usImageOrientation2 == US_IMG_ORIENT_XX ) 
  {
    LOG_ERROR("Failed to determine the necessary image flip - unknown input image orientation 2"); 
    return PLUS_SUCCESS; 
  }
  if (usImageOrientation1==usImageOrientation2)
  {
    // no flip
    return PLUS_SUCCESS;
  }

  if( (usImageOrientation1==US_IMG_ORIENT_UF && usImageOrientation2==US_IMG_ORIENT_MF) ||
      (usImageOrientation1==US_IMG_ORIENT_MF && usImageOrientation2==US_IMG_ORIENT_UF) ||
      (usImageOrientation1==US_IMG_ORIENT_UN && usImageOrientation2==US_IMG_ORIENT_MN) ||
      (usImageOrientation1==US_IMG_ORIENT_MN && usImageOrientation2==US_IMG_ORIENT_UN) ||
      (usImageOrientation1==US_IMG_ORIENT_FU && usImageOrientation2==US_IMG_ORIENT_NU) ||
      (usImageOrientation1==US_IMG_ORIENT_NU && usImageOrientation2==US_IMG_ORIENT_FU) ||
      (usImageOrientation1==US_IMG_ORIENT_FM && usImageOrientation2==US_IMG_ORIENT_NM) ||
      (usImageOrientation1==US_IMG_ORIENT_NM && usImageOrientation2==US_IMG_ORIENT_FM)
    )
  {
    // flip x
    flipInfo.hFlip=true;
    return PLUS_SUCCESS;
  }
  if( (usImageOrientation1==US_IMG_ORIENT_UF && usImageOrientation2==US_IMG_ORIENT_UN) ||
      (usImageOrientation1==US_IMG_ORIENT_MF && usImageOrientation2==US_IMG_ORIENT_MN) ||
      (usImageOrientation1==US_IMG_ORIENT_UN && usImageOrientation2==US_IMG_ORIENT_UF) ||
      (usImageOrientation1==US_IMG_ORIENT_MN && usImageOrientation2==US_IMG_ORIENT_MF) ||
      (usImageOrientation1==US_IMG_ORIENT_FU && usImageOrientation2==US_IMG_ORIENT_FM) ||
      (usImageOrientation1==US_IMG_ORIENT_NU && usImageOrientation2==US_IMG_ORIENT_NM) ||
      (usImageOrientation1==US_IMG_ORIENT_FM && usImageOrientation2==US_IMG_ORIENT_FU) ||
      (usImageOrientation1==US_IMG_ORIENT_NM && usImageOrientation2==US_IMG_ORIENT_NU)
    )
  {
    // flip y
    flipInfo.vFlip=true;
    return PLUS_SUCCESS;
  }
  if( (usImageOrientation1==US_IMG_ORIENT_UFA && usImageOrientation2==US_IMG_ORIENT_UFD) ||
      (usImageOrientation1==US_IMG_ORIENT_UFD && usImageOrientation2==US_IMG_ORIENT_UFA) ||
      (usImageOrientation1==US_IMG_ORIENT_MFA && usImageOrientation2==US_IMG_ORIENT_MFD) ||
      (usImageOrientation1==US_IMG_ORIENT_MFD && usImageOrientation2==US_IMG_ORIENT_MFA) ||
      (usImageOrientation1==US_IMG_ORIENT_UNA && usImageOrientation2==US_IMG_ORIENT_UND) ||
      (usImageOrientation1==US_IMG_ORIENT_UND && usImageOrientation2==US_IMG_ORIENT_UNA) ||
      (usImageOrientation1==US_IMG_ORIENT_MNA && usImageOrientation2==US_IMG_ORIENT_MND) ||
      (usImageOrientation1==US_IMG_ORIENT_MND && usImageOrientation2==US_IMG_ORIENT_MNA)
    )
  {
    // flip z
    flipInfo.eFlip=true;
    return PLUS_SUCCESS;
  }
  if( (usImageOrientation1==US_IMG_ORIENT_UF && usImageOrientation2==US_IMG_ORIENT_MN) ||
      (usImageOrientation1==US_IMG_ORIENT_MF && usImageOrientation2==US_IMG_ORIENT_UN) ||
      (usImageOrientation1==US_IMG_ORIENT_UN && usImageOrientation2==US_IMG_ORIENT_MF) ||
      (usImageOrientation1==US_IMG_ORIENT_MN && usImageOrientation2==US_IMG_ORIENT_UF) ||
      (usImageOrientation1==US_IMG_ORIENT_FU && usImageOrientation2==US_IMG_ORIENT_NM) ||
      (usImageOrientation1==US_IMG_ORIENT_NU && usImageOrientation2==US_IMG_ORIENT_FM) ||
      (usImageOrientation1==US_IMG_ORIENT_FM && usImageOrientation2==US_IMG_ORIENT_NU) ||
      (usImageOrientation1==US_IMG_ORIENT_NM && usImageOrientation2==US_IMG_ORIENT_FU)
    )
  {
    // flip xy
    flipInfo.hFlip=true;
    flipInfo.vFlip=true;
    return PLUS_SUCCESS;
  }
  if( (usImageOrientation1==US_IMG_ORIENT_UFA && usImageOrientation2==US_IMG_ORIENT_MFD) ||
      (usImageOrientation1==US_IMG_ORIENT_MFD && usImageOrientation2==US_IMG_ORIENT_UFA) ||
      (usImageOrientation1==US_IMG_ORIENT_UNA && usImageOrientation2==US_IMG_ORIENT_MND) ||
      (usImageOrientation1==US_IMG_ORIENT_MND && usImageOrientation2==US_IMG_ORIENT_UNA)
    )
  {
    // flip xz
    flipInfo.hFlip=true;
    flipInfo.eFlip=true;
    return PLUS_SUCCESS;
  }
  if( 
      (usImageOrientation1==US_IMG_ORIENT_UFA && usImageOrientation2==US_IMG_ORIENT_UND) ||
      (usImageOrientation1==US_IMG_ORIENT_UND && usImageOrientation2==US_IMG_ORIENT_UFA) ||
      (usImageOrientation1==US_IMG_ORIENT_MFA && usImageOrientation2==US_IMG_ORIENT_MND) ||
      (usImageOrientation1==US_IMG_ORIENT_MND && usImageOrientation2==US_IMG_ORIENT_MFA)
    )
  {
    // flip yz
    flipInfo.vFlip=true;
    flipInfo.eFlip=true;
    return PLUS_SUCCESS;
  }
  if( 
    (usImageOrientation1==US_IMG_ORIENT_UFA && usImageOrientation2==US_IMG_ORIENT_MND) ||
    (usImageOrientation1==US_IMG_ORIENT_MND && usImageOrientation2==US_IMG_ORIENT_UFA)
    )
  {
    // flip xyz
    flipInfo.hFlip=true;
    flipInfo.vFlip=true;
    flipInfo.eFlip=true;
    return PLUS_SUCCESS;
  } 

  assert(0);
  LOG_ERROR("Image orientation conversion between orientations " << PlusVideoFrame::GetStringFromUsImageOrientation(usImageOrientation1)
    << " and " << PlusVideoFrame::GetStringFromUsImageOrientation(usImageOrientation2)
    << " is not supported. Only reordering of rows, columns and slices.");
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::GetOrientedImage( vtkImageData* inUsImage, 
                                            US_IMAGE_ORIENTATION inUsImageOrientation, 
                                            US_IMAGE_TYPE inUsImageType, 
                                            US_IMAGE_ORIENTATION outUsImageOrientation, 
                                            vtkImageData* outUsOrientedImage,
                                            int* clipRectangleOrigin /*=NULL*/, 
                                            int* clipRectangleSize /*=NULL*/ )
{
  if ( inUsImage == NULL )
  {
    LOG_ERROR("Failed to convert image data to the requested orientation - input image is null!"); 
    return PLUS_FAIL; 
  }

  if ( outUsOrientedImage == NULL )
  {
    LOG_ERROR("Failed to convert image data to the requested orientation - output image is null!"); 
    return PLUS_FAIL; 
  }

  FlipInfoType flipInfo;
  if ( PlusVideoFrame::GetFlipAxes(inUsImageOrientation, inUsImageType, outUsImageOrientation, flipInfo) != PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to convert image data to the requested orientation, from " << PlusVideoFrame::GetStringFromUsImageOrientation(inUsImageOrientation) << 
      " to " << PlusVideoFrame::GetStringFromUsImageOrientation(outUsImageOrientation));
    return PLUS_FAIL;
  }
  if ( !flipInfo.hFlip && !flipInfo.vFlip && !flipInfo.eFlip )
  {
    // no flip
    outUsOrientedImage->ShallowCopy( inUsImage ); 
    return PLUS_SUCCESS; 
  }

  // TODO : out oriented image better match clip size
  int outWidth = outUsOrientedImage->GetExtent()[1] - outUsOrientedImage->GetExtent()[0];
  int inWidth = inUsImage->GetExtent()[1] - inUsImage->GetExtent()[0];
  int outHeight = outUsOrientedImage->GetExtent()[3] - outUsOrientedImage->GetExtent()[2];
  int inHeight = inUsImage->GetExtent()[3] - inUsImage->GetExtent()[2];
  int outDepth = outUsOrientedImage->GetExtent()[5] - outUsOrientedImage->GetExtent()[4];
  int inDepth = inUsImage->GetExtent()[5] - inUsImage->GetExtent()[4];

  if( outHeight != inHeight || outWidth != inWidth || outDepth != inDepth || outUsOrientedImage->GetScalarType() != inUsImage->GetScalarType() || outUsOrientedImage->GetNumberOfScalarComponents() != inUsImage->GetNumberOfScalarComponents() )
  {
    // Allocate the output image
    outUsOrientedImage->SetExtent(inUsImage->GetExtent());
#if (VTK_MAJOR_VERSION < 6)
    outUsOrientedImage->SetScalarType(inUsImage->GetScalarType());
    outUsOrientedImage->SetNumberOfScalarComponents(inUsImage->GetNumberOfScalarComponents());
    outUsOrientedImage->AllocateScalars(); 
#else
    outUsOrientedImage->AllocateScalars(inUsImage->GetScalarType(), inUsImage->GetNumberOfScalarComponents());
#endif
  }

  int numberOfBytesPerScalar = PlusVideoFrame::GetNumberOfBytesPerScalar(inUsImage->GetScalarType());

  int extent[6]={0,0,0,0,0,0}; 
  inUsImage->GetExtent(extent); 
  double width = extent[1] - extent[0] + 1; 
  double height = extent[3] - extent[2] + 1;
  double depth = extent[5] - extent[4] + 1;

  PlusStatus status(PLUS_FAIL);
  switch (numberOfBytesPerScalar)
  {
  case 1:
    status = FlipImageGeneric<vtkTypeUInt8>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
    break;
  case 2:
    status = FlipImageGeneric<vtkTypeUInt16>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
    break;
  case 4:
    status = FlipImageGeneric<vtkTypeUInt32>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
    break;
  default:
    LOG_ERROR("Unsupported bit depth: "<<numberOfBytesPerScalar<<" bytes per scalar");
  }
  return status;
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::GetOrientedImage(  unsigned char* imageDataPtr,                               
                                            US_IMAGE_ORIENTATION  inUsImageOrientation, 
                                            US_IMAGE_TYPE inUsImageType, 
                                            PlusCommon::VTKScalarPixelType pixType,
                                            int numberOfScalarComponents,  
                                            const int    frameSizeInPx[3],
                                            US_IMAGE_ORIENTATION  outUsImageOrientation, 
                                            vtkImageData* outUsOrientedImage,
                                            int* clipRectangleOrigin /*=NULL*/, 
                                            int* clipRectangleSize /*=NULL*/)
{
  if ( imageDataPtr == NULL )
  {
    LOG_ERROR("Failed to convert image data to MF orientation - input image is null!"); 
    return PLUS_FAIL; 
  }

  if ( outUsOrientedImage == NULL )
  {
    LOG_ERROR("Failed to convert image data to MF orientation - output image is null!"); 
    return PLUS_FAIL; 
  }

  // TODO : out oriented image better match clip size
  int outExtents[6] = {0,0,0,0,0,0};
  outUsOrientedImage->GetExtent(outExtents);
  if ( !(frameSizeInPx[0]-1 == outExtents[1] &&
    frameSizeInPx[1]-1 == outExtents[3] &&
    frameSizeInPx[2]-1 == outExtents[5] &&
    outUsOrientedImage->GetScalarType() == pixType &&
    outUsOrientedImage->GetNumberOfScalarComponents() == numberOfScalarComponents) )
  {
    // Allocate the output image
    PlusVideoFrame::AllocateFrame(outUsOrientedImage, frameSizeInPx, pixType, numberOfScalarComponents);
  }

  vtkImageData* inUsImage = vtkImageData::New();
  PlusVideoFrame::AllocateFrame(inUsImage, frameSizeInPx, outUsOrientedImage->GetScalarType(), outUsOrientedImage->GetNumberOfScalarComponents());
  
  memcpy(inUsImage->GetScalarPointer(), imageDataPtr, frameSizeInPx[0]*frameSizeInPx[1]*frameSizeInPx[2]*PlusVideoFrame::GetNumberOfBytesPerScalar(pixType)*numberOfScalarComponents);

  FlipInfoType flipInfo;
  if ( PlusVideoFrame::GetFlipAxes(inUsImageOrientation, inUsImageType, outUsImageOrientation, flipInfo) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to convert image data to the requested orientation, from " << GetStringFromUsImageOrientation(inUsImageOrientation) << " to " << GetStringFromUsImageOrientation(outUsImageOrientation));
    DELETE_IF_NOT_NULL(inUsImage);
    return PLUS_FAIL;
  }

  if ( !flipInfo.hFlip && !flipInfo.vFlip && !flipInfo.eFlip )
  {
    // no flip
    outUsOrientedImage->DeepCopy(inUsImage);
    DELETE_IF_NOT_NULL(inUsImage);
    return PLUS_SUCCESS; 
  }

  PlusStatus result = PlusVideoFrame::GetOrientedImage(inUsImage, inUsImageOrientation, inUsImageType, 
    outUsImageOrientation, outUsOrientedImage, clipRectangleOrigin, clipRectangleSize); 
  DELETE_IF_NOT_NULL(inUsImage);
  return result;
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::GetOrientedImage( unsigned char* imageDataPtr, 
                                            US_IMAGE_ORIENTATION  inUsImageOrientation, 
                                            US_IMAGE_TYPE inUsImageType, 
                                            PlusCommon::VTKScalarPixelType pixType, 
                                            int numberOfScalarComponents, 
                                            const int frameSizeInPx[3], 
                                            US_IMAGE_ORIENTATION outUsImageOrientation, 
                                            PlusVideoFrame &outBufferItem,
                                            int* clipRectangleOrigin /*=NULL*/, 
                                            int* clipRectangleSize /*=NULL*/)
{
  return PlusVideoFrame::GetOrientedImage(imageDataPtr, inUsImageOrientation, inUsImageType, pixType, 
    numberOfScalarComponents, frameSizeInPx, outUsImageOrientation, outBufferItem.GetImage(), clipRectangleOrigin, clipRectangleSize);
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::FlipImage(vtkImageData* inUsImage, const PlusVideoFrame::FlipInfoType& flipInfo, vtkImageData* outUsOrientedImage)
{
  if( outUsOrientedImage == NULL )
  {
    LOG_ERROR("Null output image sent to flip image. Nothing to write into.");
    return PLUS_FAIL;
  }
  int extents[6] = {0,0,0,0,0,0};
  inUsImage->GetExtent(extents);
  int frameSize[3] = {0,0,0};
  frameSize[0] = extents[1]-1;
  frameSize[1] = extents[3]-1;
  frameSize[2] = extents[5]-1;
  PlusVideoFrame::AllocateFrame(outUsOrientedImage, frameSize, inUsImage->GetScalarType(), inUsImage->GetNumberOfScalarComponents());

  outUsOrientedImage->GetExtent(extents);
  int width = extents[1] - extents[0];
  int height = extents[3] - extents[2];
  int depth = extents[5] - extents[4];
  switch(outUsOrientedImage->GetScalarType())
  {
  case VTK_UNSIGNED_CHAR: return FlipImageGeneric<vtkTypeUInt8>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_CHAR: return FlipImageGeneric<vtkTypeInt8>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_UNSIGNED_SHORT: return FlipImageGeneric<vtkTypeUInt16>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_SHORT: return FlipImageGeneric<vtkTypeInt16>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_UNSIGNED_INT: return FlipImageGeneric<vtkTypeUInt32>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_INT: return FlipImageGeneric<vtkTypeInt32>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_UNSIGNED_LONG: return FlipImageGeneric<unsigned long>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_LONG: return FlipImageGeneric<long>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_FLOAT: return FlipImageGeneric<vtkTypeFloat32>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  case VTK_DOUBLE: return FlipImageGeneric<vtkTypeFloat64>(inUsImage->GetScalarPointer(), inUsImage->GetNumberOfScalarComponents(), width, height, depth, flipInfo, outUsOrientedImage->GetScalarPointer());
  default:
    LOG_ERROR("Unknown pixel type. Cannot re-orient image.");
    return PLUS_FAIL;
  }
}

//----------------------------------------------------------------------------
PlusStatus PlusVideoFrame::ReadImageFromFile( PlusVideoFrame& frame, const char* fileName)
{
  vtkImageReader2* reader(NULL);

  std::string extension = vtksys::SystemTools::GetFilenameExtension(std::string(fileName));
  std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

  std::vector< std::string > readerExtensions;

  if( reader == NULL )
  {
    reader = vtkTIFFReader::New();
    PlusCommon::SplitStringIntoTokens(reader->GetFileExtensions(), ' ', readerExtensions);

    if( std::find(readerExtensions.begin(), readerExtensions.end(), extension) == readerExtensions.end() )
    {
      DELETE_IF_NOT_NULL(reader);
    }
  }

  if( reader == NULL )
  {
    reader = vtkBMPReader::New();
    PlusCommon::SplitStringIntoTokens(reader->GetFileExtensions(), ' ', readerExtensions);

    if( std::find(readerExtensions.begin(), readerExtensions.end(), extension) == readerExtensions.end() )
    {
      DELETE_IF_NOT_NULL(reader);
    }
  }

  if( reader == NULL )
  {
    reader = vtkPNMReader::New();
    PlusCommon::SplitStringIntoTokens(reader->GetFileExtensions(), ' ', readerExtensions);

    if( std::find(readerExtensions.begin(), readerExtensions.end(), extension) == readerExtensions.end() )
    {
      DELETE_IF_NOT_NULL(reader);
    }
  }

  reader->SetFileName(fileName);
  reader->Update();

  PlusStatus result = frame.DeepCopyFrom(reader->GetOutput());
  reader->Delete();
  return result;
}


//----------------------------------------------------------------------------
int PlusVideoFrame::GetNumberOfBytesPerScalar(PlusCommon::VTKScalarPixelType pixelType)
{
  switch (pixelType)
  {
  case VTK_UNSIGNED_CHAR: return sizeof(vtkTypeUInt8);
  case VTK_CHAR: return sizeof(vtkTypeInt8);
  case VTK_UNSIGNED_SHORT: return sizeof(vtkTypeUInt16);
  case VTK_SHORT: return sizeof(vtkTypeInt16);
  case VTK_UNSIGNED_INT: return sizeof(vtkTypeUInt32);
  case VTK_INT: return sizeof(vtkTypeInt32);
  case VTK_UNSIGNED_LONG: return sizeof(unsigned long);
  case VTK_LONG: return sizeof(long);
  case VTK_FLOAT: return sizeof(vtkTypeFloat32);
  case VTK_DOUBLE: return sizeof(vtkTypeFloat64);
  default:
    LOG_ERROR("GetNumberOfBytesPerPixel: unknown pixel type " << pixelType);
    return VTK_VOID;
  }
}

//----------------------------------------------------------------------------
PlusCommon::VTKScalarPixelType PlusVideoFrame::GetVTKScalarPixelType(PlusCommon::ITKScalarPixelType pixelType)
{
  switch (pixelType)
  {
  case itk::ImageIOBase::UCHAR: return VTK_UNSIGNED_CHAR;
  case itk::ImageIOBase::CHAR: return VTK_CHAR;
  case itk::ImageIOBase::USHORT: return VTK_UNSIGNED_SHORT;
  case itk::ImageIOBase::SHORT: return VTK_SHORT;
  case itk::ImageIOBase::UINT: return VTK_UNSIGNED_INT;
  case itk::ImageIOBase::INT: return VTK_INT;
  case itk::ImageIOBase::ULONG: return VTK_UNSIGNED_LONG;
  case itk::ImageIOBase::LONG: return VTK_LONG;
  case itk::ImageIOBase::FLOAT: return VTK_FLOAT;
  case itk::ImageIOBase::DOUBLE: return VTK_DOUBLE;
  case itk::ImageIOBase::UNKNOWNCOMPONENTTYPE:
  default:
    return VTK_VOID;      
  }
}

#ifdef PLUS_USE_OpenIGTLink
//----------------------------------------------------------------------------
// static 
PlusCommon::VTKScalarPixelType PlusVideoFrame::GetVTKScalarPixelTypeFromIGTL(PlusCommon::IGTLScalarPixelType igtlPixelType)
{
  switch (igtlPixelType)
  {
  case igtl::ImageMessage::TYPE_INT8: return VTK_CHAR;
  case igtl::ImageMessage::TYPE_UINT8: return VTK_UNSIGNED_CHAR;
  case igtl::ImageMessage::TYPE_INT16: return VTK_SHORT;
  case igtl::ImageMessage::TYPE_UINT16: return VTK_UNSIGNED_SHORT;
  case igtl::ImageMessage::TYPE_INT32: return VTK_INT;
  case igtl::ImageMessage::TYPE_UINT32: return VTK_UNSIGNED_INT;
  case igtl::ImageMessage::TYPE_FLOAT32: return VTK_FLOAT;
  case igtl::ImageMessage::TYPE_FLOAT64: return VTK_DOUBLE;
  default:
    return VTK_VOID;  
  }
}

//----------------------------------------------------------------------------
// static 
PlusCommon::IGTLScalarPixelType PlusVideoFrame::GetIGTLScalarPixelTypeFromVTK(PlusCommon::VTKScalarPixelType pixelType)
{
  switch (pixelType)
  {
  case VTK_CHAR: return igtl::ImageMessage::TYPE_INT8;
  case VTK_UNSIGNED_CHAR: return igtl::ImageMessage::TYPE_UINT8;
  case VTK_SHORT: return igtl::ImageMessage::TYPE_INT16;
  case VTK_UNSIGNED_SHORT: return igtl::ImageMessage::TYPE_UINT16;
  case VTK_INT: return igtl::ImageMessage::TYPE_INT32;
  case VTK_UNSIGNED_INT: return igtl::ImageMessage::TYPE_UINT32;
  case VTK_FLOAT: return igtl::ImageMessage::TYPE_FLOAT32;
  case VTK_DOUBLE: return igtl::ImageMessage::TYPE_FLOAT64;
  default:
    // There is no unknown IGT scalar pixel type, so display an error message 
    LOG_ERROR("Unknown conversion between VTK scalar pixel type (" << pixelType << ") and IGT pixel type - return igtl::ImageMessage::TYPE_INT8 by default!"); 
    return igtl::ImageMessage::TYPE_INT8;      
  }
}

#endif

//----------------------------------------------------------------------------
vtkImageData* PlusVideoFrame::GetImage() const
{
  return this->Image;
}

//----------------------------------------------------------------------------
void PlusVideoFrame::SetImageData( vtkImageData* imageData )
{
  this->Image = imageData;
}

//----------------------------------------------------------------------------
#define VTK_TO_STRING(pixType) case pixType: return "##pixType"
std::string PlusVideoFrame::GetStringFromVTKPixelType( PlusCommon::VTKScalarPixelType vtkScalarPixelType )
{
  switch(vtkScalarPixelType)
  {
    VTK_TO_STRING(VTK_CHAR);
    VTK_TO_STRING(VTK_UNSIGNED_CHAR);
    VTK_TO_STRING(VTK_SHORT);
    VTK_TO_STRING(VTK_UNSIGNED_SHORT);
    VTK_TO_STRING(VTK_INT);
    VTK_TO_STRING(VTK_UNSIGNED_INT);
    VTK_TO_STRING(VTK_FLOAT);
    VTK_TO_STRING(VTK_DOUBLE);
  default:
    return "Unknown";
  }
}

#undef VTK_TO_STRING