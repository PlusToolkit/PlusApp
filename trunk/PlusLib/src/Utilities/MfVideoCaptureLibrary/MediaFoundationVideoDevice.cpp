/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.

This file is subject to the Code Project Open Source License.
See http://www.codeproject.com/info/cpol10.aspx

Original work by Evgeny Pereguda
http://www.codeproject.com/Members/Evgeny-Pereguda

Original "videoInput" library at
http://www.codeproject.com/Articles/559437/Capturing-video-from-web-camera-on-Windows-7-and-8

The "videoInput" library has been adapted to fit within a namespace.

=========================================================Plus=header=end*/

#include "FormatReader.h"
#include "MediaFoundationVideoDevice.h"
#include "MfVideoCaptureLoggerMacros.h"
#include <Strmif.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <string.h>

//----------------------------------------------------------------------------

namespace
{
  template <class T> void SafeRelease(T **ppT)
  {
    if (*ppT)
    {
      (*ppT)->Release();
      *ppT = NULL;
    }
  }
}

//----------------------------------------------------------------------------

namespace MfVideoCapture
{
  MediaFoundationVideoDevice::MediaFoundationVideoDevice(void)
    : IsSetup(false)
    , LockOut(OpenLock)
    , FriendlyName(NULL)
    , Width(0)
    , Height(0)
    , FrameRate(0)
    , Source(NULL)
    , StopEventCallbackFunc(NULL)
    , DeviceIndex(-1)
    , UserData(NULL)
  {  

  }

  //----------------------------------------------------------------------------

  void MediaFoundationVideoDevice::SetParameters(CaptureDeviceParameters Parameters)
  {
    if(IsSetup)
    {
      if(Source)
      {
        unsigned int shift = sizeof(Parameter);
        Parameter *pParameter = (Parameter *)(&Parameters);
        Parameter *pPrevParameter = (Parameter *)(&PreviousParameters);

        IAMVideoProcAmp *pProcAmp = NULL;
        HRESULT hr = Source->QueryInterface(IID_PPV_ARGS(&pProcAmp));

        if (SUCCEEDED(hr))
        {
          for(unsigned int i = 0; i < 10; i++)
          {
            if(pPrevParameter[i].CurrentValue != pParameter[i].CurrentValue || pPrevParameter[i].Flag != pParameter[i].Flag)
              hr = pProcAmp->Set(VideoProcAmp_Brightness + i, pParameter[i].CurrentValue, pParameter[i].Flag);

          }

          pProcAmp->Release();
        }

        IAMCameraControl *pProcControl = NULL;
        hr = Source->QueryInterface(IID_PPV_ARGS(&pProcControl));

        if (SUCCEEDED(hr))
        {
          for(unsigned int i = 0; i < 7; i++)
          {
            if(pPrevParameter[10 + i].CurrentValue != pParameter[10 + i].CurrentValue || pPrevParameter[10 + i].Flag != pParameter[10 + i].Flag)
              hr = pProcControl->Set(CameraControl_Pan+i, pParameter[10 + i].CurrentValue, pParameter[10 + i].Flag);          
          }

          pProcControl->Release();
        }

        PreviousParameters = Parameters;
      }
    }
  }

  //----------------------------------------------------------------------------

  CaptureDeviceParameters MediaFoundationVideoDevice::GetParameters()
  {
    CaptureDeviceParameters out;

    if(IsSetup)
    {
      if(Source)
      {
        unsigned int shift = sizeof(Parameter);
        Parameter *pParameter = (Parameter *)(&out);
        IAMVideoProcAmp *pProcAmp = NULL;
        HRESULT hr = Source->QueryInterface(IID_PPV_ARGS(&pProcAmp));

        if (SUCCEEDED(hr))
        {
          for(unsigned int i = 0; i < 10; i++)
          {
            Parameter temp;

            hr = pProcAmp->GetRange(VideoProcAmp_Brightness+i, &temp.Min, &temp.Max, &temp.Step, &temp.Default, &temp.Flag);

            if (SUCCEEDED(hr))
            {
              temp.CurrentValue = temp.Default;
              pParameter[i] = temp;
            }
          }

          pProcAmp->Release();
        }

        IAMCameraControl *pProcControl = NULL;
        hr = Source->QueryInterface(IID_PPV_ARGS(&pProcControl));

        if (SUCCEEDED(hr))
        {
          for(unsigned int i = 0; i < 7; i++)
          {
            Parameter temp;

            hr = pProcControl->GetRange(CameraControl_Pan+i, &temp.Min, &temp.Max, &temp.Step, &temp.Default, &temp.Flag);

            if (SUCCEEDED(hr))
            {
              temp.CurrentValue = temp.Default;
              pParameter[10 + i] = temp;
            }
          }

          pProcControl->Release();
        }
      }
    }

    return out;
  }

  //----------------------------------------------------------------------------

  long MediaFoundationVideoDevice::ResetDevice(IMFActivate *pActivate)
  {
    HRESULT hr = -1;

    CurrentFormats.clear();

    if(FriendlyName)
      CoTaskMemFree(FriendlyName);

    FriendlyName = NULL;

    if(pActivate)
    {    
      IMFMediaSource *pSource = NULL;

      hr = pActivate->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
        &FriendlyName,
        NULL
        );


      hr = pActivate->ActivateObject(
        __uuidof(IMFMediaSource),
        (void**)&pSource
        );

      EnumerateCaptureFormats(pSource);
      BuildLibraryofTypes();

      SafeRelease(&pSource);

      if(FAILED(hr))  
      {      
        FriendlyName = NULL;

        LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": IMFMediaSource interface cannot be created.");
      }
    }

    return hr;
  }

  //----------------------------------------------------------------------------

  long MediaFoundationVideoDevice::ReadDeviceInfo(IMFActivate *pActivate, unsigned int Num)
  {
    HRESULT hr = -1;
    DeviceIndex = Num;

    hr = ResetDevice(pActivate);

    return hr;
  }

  //----------------------------------------------------------------------------

  long MediaFoundationVideoDevice::CheckDevice(IMFAttributes *pAttributes, IMFActivate **pDevice)
  {
    HRESULT hr = S_OK;
    IMFActivate **ppDevices = NULL;
    UINT32 count;
    wchar_t *newFriendlyName = NULL;

    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);

    if (SUCCEEDED(hr))
    {
      if(count > 0)
      {
        if(count > DeviceIndex)
        {      
          hr = ppDevices[DeviceIndex]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &newFriendlyName,
            NULL
            );

          if (SUCCEEDED(hr))
          {
            if(wcscmp(newFriendlyName, FriendlyName) != 0)
            {
              LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Chosen device cannot be found.");
              hr = -1;
              pDevice = NULL;
            }
            else
            {
              *pDevice = ppDevices[DeviceIndex];
              (*pDevice)->AddRef();
            }
          }
          else
          {
            LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Name of device cannot be gotten.");
          }
        }
        else
        {
          LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Number of devices more than corrent number of the device.");
          hr = -1;
        }

        for(UINT32 i = 0; i < count; i++)
        {
          SafeRelease(&ppDevices[i]);
        }

        SafeRelease(ppDevices);
      }
      else
        hr = -1;
    }
    else
    {
      LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": List of DeviceSources cannot be enumerated.");
    }

    return hr;
  }

  //----------------------------------------------------------------------------

  long MediaFoundationVideoDevice::InitDevice()
  {
    HRESULT hr = -1;
    IMFAttributes *pAttributes = NULL;
    IMFActivate * vd_pActivate= NULL;
    CoInitialize(NULL);

    hr = MFCreateAttributes(&pAttributes, 1);

    if (SUCCEEDED(hr))
    {
      hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
        );
    }  

    if (SUCCEEDED(hr))
    {
      hr = CheckDevice(pAttributes, &vd_pActivate);

      if (SUCCEEDED(hr) && vd_pActivate)
      {
        SafeRelease(&Source);

        hr = vd_pActivate->ActivateObject(
          __uuidof(IMFMediaSource),
          (void**)&Source
          );

        if (SUCCEEDED(hr))
        {

        }

        SafeRelease(&vd_pActivate);
      }
      else
      {
        LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Cannot activate device.");
      }
    }  
    else
    {
      LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": The attribute of the capture device cannot be retrieved.");
    }

    SafeRelease(&pAttributes);

    return hr;
  }

  //----------------------------------------------------------------------------
  MediaType MediaFoundationVideoDevice::GetFormat(unsigned int id) const
  {
    if(id < CurrentFormats.size())
    {
      return CurrentFormats[id];
    }
    else return MediaType();

  }

  //----------------------------------------------------------------------------

  int MediaFoundationVideoDevice::GetCountFormats() const
  {
    return CurrentFormats.size();
  }

  //----------------------------------------------------------------------------

  void MediaFoundationVideoDevice::SetEmergencyStopEvent(void *userData, void(*func)(int, void *))
  {
    StopEventCallbackFunc = func;
    UserData = userData;
  }

  //----------------------------------------------------------------------------

  void MediaFoundationVideoDevice::CloseDevice()
  {    
    if(IsSetup)
    {
      IsSetup = false;
      this->Stop();

      SafeRelease(&Source);

      LockOut = OpenLock;  

      LOG_DEBUG("VIDEODEVICE " << DeviceIndex << ": Device is stopped.");
    }
  }

  //----------------------------------------------------------------------------

  bool MediaFoundationVideoDevice::Start()
  {
    if( this->Source != NULL && IsSetup )
    {
      IMFPresentationDescriptor *pPD = NULL;

      HRESULT hr = Source->CreatePresentationDescriptor(&pPD);
      if (FAILED(hr))
      {
        return false;
      }

      Source->Start(pPD, NULL, NULL);

      SafeRelease(&pPD);
      return true;
    }
    return false;
  }

  //----------------------------------------------------------------------------

  bool MediaFoundationVideoDevice::Stop()
  {
    if( this->Source != NULL && IsSetup )
    {
      Source->Stop();
      return true;
    }
    return false;
  }

  //----------------------------------------------------------------------------

  unsigned int MediaFoundationVideoDevice::GetWidth() const
  {
    if(IsSetup)
      return Width;
    else
      return 0;
  }

  //----------------------------------------------------------------------------

  unsigned int MediaFoundationVideoDevice::GetHeight() const
  {
    if(IsSetup)
      return Height;
    else 
      return 0;
  }

  //----------------------------------------------------------------------------

  unsigned int MediaFoundationVideoDevice::GetFrameRate() const
  {
    if(IsSetup)
      return FrameRate;
    else
      return 0;
  }

  //----------------------------------------------------------------------------

  IMFMediaSource *MediaFoundationVideoDevice::GetMediaSource()
  {
    IMFMediaSource *out = NULL;

    if(LockOut == OpenLock)
    {
      LockOut = MediaSourceLock;      

      out = Source;
    }

    return out;
  }

  //----------------------------------------------------------------------------

  int MediaFoundationVideoDevice::FindType(unsigned int size, unsigned int frameRate, GUID subtype, MFVideoInterlaceMode interlaceMode)
  {  
    if(CaptureFormats.size() == 0)
      return 0;

    FrameRateToSubTypeMap rateToSubType;
    if( CaptureFormats.find(size) == CaptureFormats.end() )
    {
      return -1;
    }
    else
    {
      rateToSubType = CaptureFormats[size];
    }

    if(rateToSubType.size() == 0)
      return -1;
    
    SubTypeNameToIdsMap nameToIdsMaxFrameRate;

    if(frameRate == 0)
    {
      // find the format with the maximum frame rate
      UINT64 frameRateMax = 0;
      FrameRateToSubTypeMap::iterator f = rateToSubType.begin();
      for(; f != rateToSubType.end(); f++)
      {
        if((*f).first >= frameRateMax)
        {
          frameRateMax = (*f).first;

          nameToIdsMaxFrameRate = (*f).second;
        }
      }
    }
    else
    {
      // find the format that is the closest to the requested frame rate
      FrameRateToSubTypeMap::iterator f = rateToSubType.begin();
      int frameRateDifferenceMin = -1;

      for(; f != rateToSubType.end(); f++)
      {
        int frameRateDifference=static_cast<int>((*f).first)-frameRate;
        if ( (frameRateDifferenceMin<0) || (frameRateDifference<frameRateDifferenceMin) )
        {
          frameRateDifferenceMin = frameRateDifference;
          nameToIdsMaxFrameRate = (*f).second;
        }
      }
    }

    if(nameToIdsMaxFrameRate.size() == 0)
    {
      LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": No pixel formats available.");
      return -1;
    }

    std::wstring subtypeName = FormatReader::StringFromGUID(subtype);

    VectorOfTypeIDs idList;
    FrameRateToSubTypeMap::iterator selectedSubtype;
    if( nameToIdsMaxFrameRate.find(subtypeName) == nameToIdsMaxFrameRate.end() )
    {
      LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Requested pixel format not available. Defaulting to first available.");
      idList = nameToIdsMaxFrameRate.begin()->second;
    }
    else
    {
      idList = nameToIdsMaxFrameRate[subtypeName];
    }

    if(idList.size() == 0)
    {
      LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": List of IDs has not been populated.");
      return -1;
    }

    return idList[0];

  }

  //----------------------------------------------------------------------------

  void MediaFoundationVideoDevice::BuildLibraryofTypes()
  {
    unsigned int size;
    unsigned int framerate;
    int count = 0;

    for(std::vector<MediaType>::iterator i = CurrentFormats.begin(); i != CurrentFormats.end(); ++i)
    {
      size = (*i).MF_MT_FRAME_SIZE;

      framerate = (*i).MF_MT_FRAME_RATE;

      FrameRateToSubTypeMap rateToSubType = CaptureFormats[size];
      SubTypeNameToIdsMap nameToId = rateToSubType[framerate];
      std::wstring subTypeName = (*i).MF_MT_SUBTYPEName;
      VectorOfTypeIDs idList = nameToId[subTypeName];

      idList.push_back(count);
      nameToId[subTypeName] = idList;
      rateToSubType[framerate] = nameToId;
      CaptureFormats[size] = rateToSubType;

      count++;
    }
  }

  //----------------------------------------------------------------------------

  long MediaFoundationVideoDevice::SetDeviceFormat(IMFMediaSource *pSource, unsigned long  dwFormatIndex)
  {
    IMFPresentationDescriptor *pPD = NULL;
    IMFStreamDescriptor *pSD = NULL;
    IMFMediaTypeHandler *pHandler = NULL;
    IMFMediaType *pType = NULL;

    HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
    if (FAILED(hr))
    {
      goto done;
    }

    BOOL fSelected;
    hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
    if (FAILED(hr))
    {
      goto done;
    }

    hr = pSD->GetMediaTypeHandler(&pHandler);
    if (FAILED(hr))
    {
      goto done;
    }

    hr = pHandler->GetMediaTypeByIndex((DWORD)dwFormatIndex, &pType);
    if (FAILED(hr))
    {
      goto done;
    }

    hr = pHandler->SetCurrentMediaType(pType);

done:
    SafeRelease(&pPD);
    SafeRelease(&pSD);
    SafeRelease(&pHandler);
    SafeRelease(&pType);
    return hr;
  }

  //----------------------------------------------------------------------------

  bool MediaFoundationVideoDevice::IsDeviceSetup() const
  {
    return IsSetup;
  }

  //----------------------------------------------------------------------------

  bool MediaFoundationVideoDevice::IsDeviceMediaSource() const
  {
    if(LockOut == MediaSourceLock) return true;

    return false;
  }

  //----------------------------------------------------------------------------

  bool MediaFoundationVideoDevice::IsDeviceRawDataSource() const
  {
    if(LockOut == RawDataLock) return true;

    return false;
  }

  //----------------------------------------------------------------------------

  bool MediaFoundationVideoDevice::SetupDevice(unsigned int id)
  {  
    if(!IsSetup)
    {
      HRESULT hr = -1;

      hr = InitDevice();

      if(SUCCEEDED(hr))
      {      
        Width = CurrentFormats[id].width; 
        Height = CurrentFormats[id].height;
        FrameRate = CurrentFormats[id].MF_MT_FRAME_RATE;
        hr = SetDeviceFormat(Source, (DWORD) id);
        IsSetup = (SUCCEEDED(hr));

        if(IsSetup)
        {
          LOG_DEBUG("VIDEODEVICE " << DeviceIndex << ": Device is setup.");
        }
        PreviousParameters = GetParameters();
        this->ActiveType = id;
        return IsSetup;
      }
      else
      {
        LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Interface IMFMediaSource cannot be retrieved.");
        return false;
      }
    }
    else
    {
      LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Device is already setup.");
      return false;
    }  
  }

  //----------------------------------------------------------------------------

  bool MediaFoundationVideoDevice::SetupDevice(unsigned int w, unsigned int h, unsigned int idealFramerate, GUID subtype)
  {  
    int id = FindType(w * h, idealFramerate, subtype);

    if( id == -1 )
    {
      LOG_ERROR("VIDEODEVICE " << DeviceIndex << ": Cannot find the requested type.");
      return false;
    }

    return SetupDevice(id);
  }

  //----------------------------------------------------------------------------

  wchar_t *MediaFoundationVideoDevice::GetName()
  {
    return FriendlyName;
  }

  //----------------------------------------------------------------------------

  MediaFoundationVideoDevice::~MediaFoundationVideoDevice(void)
  {    
    CloseDevice();

    SafeRelease(&Source);

    if(FriendlyName)
      CoTaskMemFree(FriendlyName);
  }

  //----------------------------------------------------------------------------

  long MediaFoundationVideoDevice::EnumerateCaptureFormats(IMFMediaSource *pSource)
  {
    IMFPresentationDescriptor *pPD = NULL;
    IMFStreamDescriptor *pSD = NULL;
    IMFMediaTypeHandler *pHandler = NULL;
    IMFMediaType *pType = NULL;

    HRESULT hr=-1;
    if (pSource==NULL)
    {
      goto done;
    }

    hr = pSource->CreatePresentationDescriptor(&pPD);
    if (FAILED(hr))
    {
      goto done;
    }

    BOOL fSelected;
    hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
    if (FAILED(hr))
    {
      goto done;
    }

    hr = pSD->GetMediaTypeHandler(&pHandler);
    if (FAILED(hr))
    {
      goto done;
    }

    DWORD cTypes = 0;
    hr = pHandler->GetMediaTypeCount(&cTypes);
    if (FAILED(hr))
    {
      goto done;
    }

    for (DWORD i = 0; i < cTypes; i++)
    {
      hr = pHandler->GetMediaTypeByIndex(i, &pType);

      if (FAILED(hr))
      {
        goto done;
      }

      MediaType MT = FormatReader::Read(pType);

      CurrentFormats.push_back(MT);

      SafeRelease(&pType);
    }

done:
    SafeRelease(&pPD);
    SafeRelease(&pSD);
    SafeRelease(&pHandler);
    SafeRelease(&pType);
    return hr;
  }

  //----------------------------------------------------------------------------

  unsigned int MediaFoundationVideoDevice::GetActiveType() const
  {
    return this->ActiveType;
  }
}