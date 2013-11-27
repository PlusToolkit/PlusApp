#include "PlusConfigure.h"
#include "PlusMath.h"
#include "vtkTrackerBuffer.h"
#include "vtkMath.h"
#include "vtkMatrix4x4.h"
#include "vtkDoubleArray.h"
#include "vtkIntArray.h"
#include "vtkUnsignedLongLongArray.h"
#include "vtkCriticalSection.h"
#include "vtkObjectFactory.h"

static const double NEGLIGIBLE_TIME_DIFFERENCE=0.00001; // in seconds, used for comparing between exact timestamps

///----------------------------------------------------------------------------
//						TrackerBufferItem
//----------------------------------------------------------------------------
TrackerBufferItem::TrackerBufferItem()
{
  this->Matrix = vtkSmartPointer<vtkMatrix4x4>::New(); 
  this->Status = TR_OK; 
}

//----------------------------------------------------------------------------
TrackerBufferItem::~TrackerBufferItem()
{
}

//----------------------------------------------------------------------------
TrackerBufferItem::TrackerBufferItem(const TrackerBufferItem &trackerBufferItem)
{
  this->Matrix = vtkSmartPointer<vtkMatrix4x4>::New(); 
  this->Status = TR_OK; 
  *this = trackerBufferItem; 
}

//----------------------------------------------------------------------------
TrackerBufferItem& TrackerBufferItem::operator=(TrackerBufferItem const& trackerBufferItem)
{
  // Handle self-assignment
  if (this == &trackerBufferItem)
  {
    return *this;
  }

  this->Status = trackerBufferItem.Status; 
  this->Matrix->DeepCopy( trackerBufferItem.Matrix ); 

  this->FilteredTimeStamp = trackerBufferItem.FilteredTimeStamp; 
  this->UnfilteredTimeStamp = trackerBufferItem.UnfilteredTimeStamp; 
  this->Index = trackerBufferItem.Index; 
  this->Uid = trackerBufferItem.Uid; 

  return *this;
}

//----------------------------------------------------------------------------
PlusStatus TrackerBufferItem::DeepCopy(TrackerBufferItem* trackerBufferItem)
{
  if ( trackerBufferItem == NULL )
  {
    LOG_ERROR("Failed to deep copy tracker buffer item - buffer item NULL!"); 
    return PLUS_FAIL; 
  }

  this->Status = trackerBufferItem->Status; 
  this->Matrix->DeepCopy( trackerBufferItem->Matrix ); 
  this->FilteredTimeStamp = trackerBufferItem->FilteredTimeStamp; 
  this->UnfilteredTimeStamp = trackerBufferItem->UnfilteredTimeStamp; 
  this->Index = trackerBufferItem->Index; 
  this->Uid = trackerBufferItem->Uid; 

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
PlusStatus TrackerBufferItem::SetMatrix(vtkMatrix4x4* matrix)
{
  if ( matrix == NULL ) 
  {
    LOG_ERROR("Failed to set matrix - input matrix is NULL!"); 
    return PLUS_FAIL; 
  }

  this->Matrix->DeepCopy(matrix); 

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
PlusStatus TrackerBufferItem::GetMatrix(vtkMatrix4x4* outputMatrix)
{
  if ( outputMatrix == NULL ) 
  {
    LOG_ERROR("Failed to copy matrix - output matrix is NULL!"); 
    return PLUS_FAIL; 
  }

  outputMatrix->DeepCopy(this->Matrix);

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
//						vtkTrackerBuffer
//----------------------------------------------------------------------------
vtkTrackerBuffer* vtkTrackerBuffer::New()
{
  // First try to create the object from the vtkObjectFactory
  vtkObject* ret = vtkObjectFactory::CreateInstance("vtkTrackerBuffer");
  if(ret)
  {
    return (vtkTrackerBuffer*)ret;
  }
  // If the factory was unable to create the object, then create it here.
  return new vtkTrackerBuffer;
}

//----------------------------------------------------------------------------
vtkTrackerBuffer::vtkTrackerBuffer()
{
  this->TrackerBuffer = vtkTimestampedCircularBuffer<TrackerBufferItem>::New(); 
  this->ToolCalibrationMatrix = NULL;
  this->WorldCalibrationMatrix = NULL;
  this->SetMaxAllowedTimeDifference(0.5); 
  this->SetBufferSize(500); 
}

//----------------------------------------------------------------------------
void vtkTrackerBuffer::DeepCopy(vtkTrackerBuffer *buffer)
{
  LOG_TRACE("vtkTrackerBuffer::DeepCopy");
  this->SetBufferSize(buffer->GetBufferSize());
  this->TrackerBuffer->DeepCopy( buffer->TrackerBuffer ); 
  this->SetToolCalibrationMatrix( buffer->GetToolCalibrationMatrix() ); 
  this->SetWorldCalibrationMatrix(buffer->GetWorldCalibrationMatrix());
}

//----------------------------------------------------------------------------
vtkTrackerBuffer::~vtkTrackerBuffer()
{
  this->SetToolCalibrationMatrix(NULL); 
  this->SetWorldCalibrationMatrix(NULL); 

  if ( this->TrackerBuffer != NULL )
  {
    this->TrackerBuffer->Delete(); 
    this->TrackerBuffer = NULL; 
  }
}

//----------------------------------------------------------------------------
void vtkTrackerBuffer::PrintSelf(ostream& os, vtkIndent indent)
{
  vtkObject::PrintSelf(os,indent);

  os << indent << "TrackerBuffer: " << this->TrackerBuffer << "\n";
  if ( this->TrackerBuffer )
  {
    this->TrackerBuffer->PrintSelf(os,indent.GetNextIndent());
  }

  os << indent << "ToolCalibrationMatrix: " << this->ToolCalibrationMatrix << "\n";
  if (this->ToolCalibrationMatrix)
  {
    this->ToolCalibrationMatrix->PrintSelf(os,indent.GetNextIndent());
  }  
  os << indent << "WorldCalibrationMatrix: " << this->WorldCalibrationMatrix << "\n";
  if (this->WorldCalibrationMatrix)
  {
    this->WorldCalibrationMatrix->PrintSelf(os,indent.GetNextIndent());
  }

}

//----------------------------------------------------------------------------
int vtkTrackerBuffer::GetBufferSize()
{
  return this->TrackerBuffer->GetBufferSize(); 
}

//----------------------------------------------------------------------------
PlusStatus vtkTrackerBuffer::SetBufferSize(int bufsize)
{
  return this->TrackerBuffer->SetBufferSize(bufsize); 
}

//----------------------------------------------------------------------------
void vtkTrackerBuffer::SetAveragedItemsForFiltering(int averagedItemsForFiltering)
{
  this->TrackerBuffer->SetAveragedItemsForFiltering(averagedItemsForFiltering); 
}

//----------------------------------------------------------------------------
void vtkTrackerBuffer::SetStartTime( double startTime)
{
  this->TrackerBuffer->SetStartTime(startTime); 
}

//----------------------------------------------------------------------------
double vtkTrackerBuffer::GetStartTime()
{
  return this->TrackerBuffer->GetStartTime(); 
}

//----------------------------------------------------------------------------
PlusStatus vtkTrackerBuffer::GetTimeStampReportTable(vtkTable* timeStampReportTable) 
{
  return this->TrackerBuffer->GetTimeStampReportTable(timeStampReportTable); 
}

//----------------------------------------------------------------------------
PlusStatus vtkTrackerBuffer::AddTimeStampedItem(vtkMatrix4x4 *matrix, TrackerStatus status, unsigned long frameNumber, double unfilteredTimestamp)
{
  double filteredTimestamp(0); 
  bool filteredTimestampProbablyValid=true;
  if ( this->TrackerBuffer->CreateFilteredTimeStampForItem(frameNumber, unfilteredTimestamp, filteredTimestamp, filteredTimestampProbablyValid) != PLUS_SUCCESS )
  {
    LOG_DEBUG("Failed to create filtered timestamp for tracker buffer item with item index: " << frameNumber); 
    return PLUS_FAIL; 
  }
  if (!filteredTimestampProbablyValid)
  {
    LOG_INFO("Filtered timestamp is probably invalid for video buffer item with item index=" << frameNumber << ", time="<<unfilteredTimestamp<<". The item may have been tagged with an inaccurate timestamp, therefore it will not be recorded." ); 
    return PLUS_SUCCESS;
  }
  return this->AddTimeStampedItem(matrix, status, frameNumber, unfilteredTimestamp, filteredTimestamp); 

}

//----------------------------------------------------------------------------
PlusStatus vtkTrackerBuffer::AddTimeStampedItem(vtkMatrix4x4 *matrix, TrackerStatus status, unsigned long frameNumber, double unfilteredTimestamp, double filteredTimestamp)
{

  if ( matrix  == NULL )
  {
    LOG_ERROR( "vtkTrackerBuffer: Unable to add NULL matrix to tracker buffer!"); 
    return PLUS_FAIL; 
  }

  int bufferIndex(0); 
  BufferItemUidType itemUid; 

  PlusLockGuard<TrackerBufferType> trackerBufferGuardedLock(this->TrackerBuffer);
  if ( this->TrackerBuffer->PrepareForNewItem(filteredTimestamp, itemUid, bufferIndex) != PLUS_SUCCESS )
  {
    // Just a debug message, because we want to avoid unnecessary warning messages if the timestamp is the same as last one
    LOG_DEBUG( "vtkTrackerBuffer: Failed to prepare for adding new frame to tracker buffer!"); 
    return PLUS_FAIL; 
  }

  // get the pointer to the correct location in the tracker buffer, where this data needs to be copied
  TrackerBufferItem* newObjectInBuffer = this->TrackerBuffer->GetBufferItemFromBufferIndex(bufferIndex); 
  if ( newObjectInBuffer == NULL )
  {
    LOG_ERROR( "vtkTrackerBuffer: Failed to get pointer to tracker buffer object from the tracker buffer for the new frame!"); 
    return PLUS_FAIL; 
  }

  PlusStatus itemStatus = newObjectInBuffer->SetMatrix(matrix);
  newObjectInBuffer->SetStatus( status ); 
  newObjectInBuffer->SetFilteredTimestamp( filteredTimestamp ); 
  newObjectInBuffer->SetUnfilteredTimestamp( unfilteredTimestamp ); 
  newObjectInBuffer->SetIndex( frameNumber ); 
  newObjectInBuffer->SetUid( itemUid ); 

  return itemStatus; 
}

//----------------------------------------------------------------------------
ItemStatus vtkTrackerBuffer::GetLatestTimeStamp( double& latestTimestamp )
{
  return this->TrackerBuffer->GetLatestTimeStamp(latestTimestamp); 
}

//----------------------------------------------------------------------------
ItemStatus vtkTrackerBuffer::GetOldestTimeStamp( double& oldestTimestamp )
{
  return this->TrackerBuffer->GetOldestTimeStamp(oldestTimestamp); 
}

//----------------------------------------------------------------------------
ItemStatus vtkTrackerBuffer::GetTimeStamp( BufferItemUidType uid, double& timestamp)
{
  return this->TrackerBuffer->GetTimeStamp(uid, timestamp); 
}

//----------------------------------------------------------------------------
ItemStatus vtkTrackerBuffer::GetTrackerBufferItem(BufferItemUidType uid, TrackerBufferItem* bufferItem, bool calibratedItem /*= false*/)
{
  if ( bufferItem == NULL )
  {
    LOG_ERROR("Unable to copy tracker buffer item into a NULL tracker buffer item!"); 
    return ITEM_UNKNOWN_ERROR; 
  }

  ItemStatus status = this->TrackerBuffer->GetFrameStatus(uid); 
  if ( status != ITEM_OK )
  {
    if (  status == ITEM_NOT_AVAILABLE_ANYMORE )
    {
      LOG_WARNING("Failed to get tracker buffer item: tracker item not available anymore"); 
    }
    else if (  status == ITEM_NOT_AVAILABLE_YET )
    {
      LOG_WARNING("Failed to get tracker buffer item: tracker item not available yet"); 
    }
    else
    {
      LOG_WARNING("Failed to get tracker buffer item!"); 
    }
    return status; 
  }

  TrackerBufferItem* trackerItem = this->TrackerBuffer->GetBufferItemFromUid(uid); 

  if ( bufferItem->DeepCopy(trackerItem) != PLUS_SUCCESS )
  {
    LOG_WARNING("Failed to copy tracker item!"); 
    return ITEM_UNKNOWN_ERROR; 
  }

  // Apply Tool calibration and World calibration matrix to tool matrix if desired 
  if ( calibratedItem ) 
  {

    vtkSmartPointer<vtkMatrix4x4> toolMatrix=vtkSmartPointer<vtkMatrix4x4>::New();
    if (trackerItem->GetMatrix(toolMatrix)!=PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to get toolMatrix"); 
      return ITEM_UNKNOWN_ERROR;
    }

    if (this->ToolCalibrationMatrix)
    {
      vtkMatrix4x4::Multiply4x4(toolMatrix, this->ToolCalibrationMatrix, toolMatrix);
    }

    if (this->WorldCalibrationMatrix)
    {
      vtkMatrix4x4::Multiply4x4(this->WorldCalibrationMatrix, toolMatrix, toolMatrix);
    }
    bufferItem->SetMatrix(toolMatrix); 
  }

  // Check the status again to make sure the writer didn't change it
  return this->TrackerBuffer->GetFrameStatus(uid); 
}

//----------------------------------------------------------------------------
// Returns the two buffer items that are closest previous and next buffer items relative to the specified time.
// itemA is the closest item
PlusStatus vtkTrackerBuffer::GetPrevNextBufferItemFromTime(double time, TrackerBufferItem& itemA, TrackerBufferItem& itemB, bool calibratedItem /*= false*/)
{
  PlusLockGuard<TrackerBufferType> trackerBufferGuardedLock(this->TrackerBuffer);

  // The returned item is computed by interpolation between itemA and itemB in time. The itemA is the closest item to the requested time.
  // Accept itemA (the closest item) as is if it is very close to the requested time.
  // Accept interpolation between itemA and itemB if all the followings are true:
  //   - both itemA and itemB exist and are valid  
  //   - time difference between the requested time and itemA is below a threshold
  //   - time difference between the requested time and itemB is below a threshold

  // itemA is the item that is the closest to the requested time, get its UID and time
  BufferItemUidType itemAuid(0); 
  ItemStatus status = this->TrackerBuffer->GetItemUidFromTime(time, itemAuid); 
  if ( status != ITEM_OK )
  {
    LOG_DEBUG("vtkTrackerBuffer: Cannot get any item from the tracker buffer for time: " << std::fixed << time <<". Probably the buffer is empty.");
    return PLUS_FAIL;
  }
  status = this->GetTrackerBufferItem(itemAuid, &itemA, calibratedItem); 
  if ( status != ITEM_OK )
  {
    LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer item with Uid: " << itemAuid );
    return PLUS_FAIL;
  }

  // If tracker is out of view, etc. then we don't have a valid before and after the requested time, so we cannot do interpolation
  if (itemA.GetStatus()!=TR_OK)
  {
    // tracker is out of view, ...
    LOG_DEBUG("vtkTrackerBuffer: Cannot do tracker data interpolation. The closest item to the requested time (time: " << std::fixed << time <<", uid: "<<itemAuid<<") is invalid.");
    return PLUS_FAIL;
  }

  double itemAtime(0);
  status = this->TrackerBuffer->GetTimeStamp(itemAuid, itemAtime); 
  if ( status != ITEM_OK )
  {
    LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer timestamp (time: " << std::fixed << time <<", uid: "<<itemAuid<<")" ); 
    return PLUS_FAIL;
  }

  // If the time difference is negligible then don't interpolate, just return the closest item
  if (fabs(itemAtime-time)<NEGLIGIBLE_TIME_DIFFERENCE)
  {
    //No need for interpolation, it's very close to the closest element
    itemB.DeepCopy(&itemA);
    return PLUS_SUCCESS;
  }  

  // If the closest item is too far, then we don't do interpolation 
  if ( fabs(itemAtime-time)>this->GetMaxAllowedTimeDifference() )
  {
    LOG_ERROR("vtkTrackerBuffer: Cannot perform interpolation, time difference compared to itemA is too big " << std::fixed << fabs(itemAtime-time) << " ( closest item time: " << itemAtime << ", requested time: " << time << ")." );
    return PLUS_FAIL;
  }

  // Find the closest item on the other side of the timescale (so that time is between itemAtime and itemBtime) 
  BufferItemUidType itemBuid(0);
  if (time < itemAtime)
  {
    // itemBtime < time <itemAtime
    itemBuid=itemAuid-1;
  }
  else
  {
    // itemAtime < time <itemBtime
    itemBuid=itemAuid+1;
  }
  if (itemBuid<this->GetOldestItemUidInBuffer()
    || itemBuid>this->GetLatestItemUidInBuffer())
  {
    // itemB is not available
    LOG_ERROR("vtkTrackerBuffer: Cannot perform interpolation, itemB is not available " << std::fixed << " ( itemBuid: " << itemBuid << ", oldest UID: " << this->GetOldestItemUidInBuffer() << ", latest UID: " << this->GetLatestItemUidInBuffer() );
    return PLUS_FAIL;
  }
  // Get item B details
  double itemBtime(0);
  status = this->TrackerBuffer->GetTimeStamp(itemBuid, itemBtime); 
  if ( status != ITEM_OK )
  {
    LOG_ERROR("Cannot do interpolation: Failed to get tracker buffer timestamp with Uid: " << itemBuid ); 
    return PLUS_FAIL;
  }
  // If the next closest item is too far, then we don't do interpolation 
  if ( fabs(itemBtime-time)>this->GetMaxAllowedTimeDifference() )
  {
    LOG_ERROR("vtkTrackerBuffer: Cannot perform interpolation, time difference compared to itemB is too big " << std::fixed << fabs(itemBtime-time) << " ( itemBtime: " << itemBtime << ", requested time: " << time << ")." );
    return PLUS_FAIL;
  }
  // Get the item
  status = this->GetTrackerBufferItem(itemBuid, &itemB, calibratedItem); 
  if ( status != ITEM_OK )
  {
    LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer item with Uid: " << itemBuid ); 
    return PLUS_FAIL;
  }
  // If there is no valid element on the other side of the requested time, then we cannot do an interpolation
  if ( itemB.GetStatus() != TR_OK )
  {
    LOG_DEBUG("vtkTrackerBuffer: Cannot get a second element (uid="<<itemBuid<<") on the other side of the requested time ("<< std::fixed << time <<")");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
ItemStatus vtkTrackerBuffer::GetTrackerBufferItemFromTime( double time, TrackerBufferItem* bufferItem, TrackerItemTemporalInterpolationType interpolation, bool calibratedItem /*= false*/)
{
  switch (interpolation)
  {
  case EXACT_TIME:
    return GetTrackerBufferItemFromExactTime(time, bufferItem, calibratedItem); 
  case INTERPOLATED:
    return GetInterpolatedTrackerBufferItemFromTime(time, bufferItem, calibratedItem); 
  default:
    LOG_ERROR("Unknown interpolation type: " <<interpolation);
    return ITEM_UNKNOWN_ERROR;
  }
}

//---------------------------------------------------------------------------- 
ItemStatus vtkTrackerBuffer::GetTrackerBufferItemFromExactTime( double time, TrackerBufferItem* bufferItem, bool calibratedItem)
{
  ItemStatus status=GetTrackerBufferItemFromClosestTime(time, bufferItem, calibratedItem);
  if ( status != ITEM_OK )
  {
    LOG_WARNING("vtkTrackerBuffer: Failed to get tracker buffer timestamp (time: " << std::fixed << time <<")" ); 
    return status;
  }

  double itemTime(0);
  BufferItemUidType uid=bufferItem->GetUid();
  status = this->TrackerBuffer->GetTimeStamp(uid, itemTime); 
  if ( status != ITEM_OK )
  {
    LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer timestamp (time: " << std::fixed << time <<", UID: "<<uid<<")" ); 
    return status;
  }

  // If the time difference is negligible then don't interpolate, just return the closest item
  if (fabs(itemTime-time)>NEGLIGIBLE_TIME_DIFFERENCE)
  {
    LOG_WARNING("vtkTrackerBuffer: Cannot find an item exactly at the requested time (requested time: " << std::fixed << time <<", item time: "<<itemTime<<")" ); 
    return ITEM_UNKNOWN_ERROR;
  }

  return status;
}

//----------------------------------------------------------------------------
ItemStatus vtkTrackerBuffer::GetTrackerBufferItemFromClosestTime( double time, TrackerBufferItem* bufferItem, bool calibratedItem)
{
  PlusLockGuard<TrackerBufferType> trackerBufferGuardedLock(this->TrackerBuffer);

  BufferItemUidType itemUid(0); 
  ItemStatus status = this->TrackerBuffer->GetItemUidFromTime(time, itemUid); 
  if ( status != ITEM_OK )
  {
    LOG_WARNING("vtkTrackerBuffer: Cannot get any item from the tracker buffer for time: " << std::fixed << time <<". Probably the buffer is empty.");
    return status;
  }

  status = this->GetTrackerBufferItem(itemUid, bufferItem, calibratedItem); 
  if ( status != ITEM_OK )
  {
    LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer item with Uid: " << itemUid );
    return status;
  }

  return status;

}


//----------------------------------------------------------------------------
// Interpolate the matrix for the given timestamp from the two nearest
// transforms in the buffer.
// The rotation is interpolated with SLERP interpolation, and the
// position is interpolated with linear interpolation.
// The flags correspond to the closest element.
ItemStatus vtkTrackerBuffer::GetInterpolatedTrackerBufferItemFromTime( double time, TrackerBufferItem* bufferItem, bool calibratedItem)
{
  TrackerBufferItem itemA; 
  TrackerBufferItem itemB; 

  if (GetPrevNextBufferItemFromTime(time, itemA, itemB, calibratedItem)!=PLUS_SUCCESS)
  {
    // cannot get two neighbors, so cannot do interpolation
    // it may be normal (e.g., when tracker out of view), so don't return with an error   
    ItemStatus status = GetTrackerBufferItemFromClosestTime(time, bufferItem, calibratedItem);
    if ( status != ITEM_OK )
    {
      LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer timestamp (time: " << std::fixed << time << ")" ); 
      return status;
    }
    bufferItem->SetStatus(TR_MISSING); // if we return at any point due to an error then it means that the interpolation is not successful, so the item is missing
    return ITEM_OK;
  }

  if (itemA.GetUid()==itemB.GetUid())
  {
    // exact match, no need for interpolation
    bufferItem->DeepCopy(&itemA);
    return ITEM_OK;
  }

  //============== Get item weights ==================

  double itemAtime(0);
  if ( this->TrackerBuffer->GetTimeStamp(itemA.GetUid(), itemAtime) != ITEM_OK )
  {
    LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer timestamp (time: " << std::fixed << time <<", uid: "<<itemA.GetUid()<<")" ); 
    return ITEM_UNKNOWN_ERROR;
  }

  double itemBtime(0);   
  if ( this->TrackerBuffer->GetTimeStamp(itemB.GetUid(), itemBtime) != ITEM_OK )
  {
    LOG_ERROR("vtkTrackerBuffer: Failed to get tracker buffer timestamp (time: " << std::fixed << time <<", uid: "<<itemB.GetUid()<<")" ); 
    return ITEM_UNKNOWN_ERROR;
  }

  double itemAweight=fabs(itemBtime-time)/fabs(itemAtime-itemBtime);
  double itemBweight=1-itemAweight;

  //============== Get transform matrices ==================

  vtkSmartPointer<vtkMatrix4x4> itemAmatrix=vtkSmartPointer<vtkMatrix4x4>::New();
  if (itemA.GetMatrix(itemAmatrix)!=PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get item A matrix"); 
    return ITEM_UNKNOWN_ERROR;
  }
  double matrixA[3][3] = {0};
  double xyzA[3] = {0};
  for (int i = 0; i < 3; i++)
  {
    matrixA[i][0] = itemAmatrix->GetElement(i,0);
    matrixA[i][1] = itemAmatrix->GetElement(i,1);
    matrixA[i][2] = itemAmatrix->GetElement(i,2);
    xyzA[i] = itemAmatrix->GetElement(i,3);
  }  

  vtkSmartPointer<vtkMatrix4x4> itemBmatrix=vtkSmartPointer<vtkMatrix4x4>::New();
  if (itemB.GetMatrix(itemBmatrix)!=PLUS_SUCCESS)
  {
    LOG_ERROR("Failed to get item B matrix"); 
    return ITEM_UNKNOWN_ERROR;
  }
  double matrixB[3][3] = {0};
  double xyzB[3] = {0};
  for (int i = 0; i < 3; i++)
  {
    matrixB[i][0] = itemBmatrix->GetElement(i,0);
    matrixB[i][1] = itemBmatrix->GetElement(i,1);
    matrixB[i][2] = itemBmatrix->GetElement(i,2);
    xyzB[i] = itemBmatrix->GetElement(i,3);
  }

  //============== Interpolate rotation ==================

  double matrixAquat[4]= {0};
  vtkMath::Matrix3x3ToQuaternion(matrixA, matrixAquat);
  double matrixBquat[4]= {0};
  vtkMath::Matrix3x3ToQuaternion(matrixB, matrixBquat);
  double interpolatedRotationQuat[4]= {0};
  PlusMath::Slerp(interpolatedRotationQuat, itemBweight, matrixAquat, matrixBquat, false);
  double interpolatedRotation[3][3] = {0};
  vtkMath::QuaternionToMatrix3x3(interpolatedRotationQuat, interpolatedRotation);

  vtkSmartPointer<vtkMatrix4x4> interpolatedMatrix = vtkSmartPointer<vtkMatrix4x4>::New(); 
  for (int i = 0; i < 3; i++)
  {
    interpolatedMatrix->Element[i][0] = interpolatedRotation[i][0];
    interpolatedMatrix->Element[i][1] = interpolatedRotation[i][1];
    interpolatedMatrix->Element[i][2] = interpolatedRotation[i][2];
    interpolatedMatrix->Element[i][3] = xyzA[i]*itemAweight + xyzB[i]*itemBweight;
    //fprintf(stderr, "%f %f %f %f\n", xyz0[i], xyz1[i],  matrix->Element[i][3], f);
  } 

  //============== Interpolate time ==================

  double itemAunfilteredTimestamp = itemA.GetUnfilteredTimestamp(0); 
  double itemBunfilteredTimestamp = itemB.GetUnfilteredTimestamp(0); 
  double interpolatedUnfilteredTimestamp = itemAunfilteredTimestamp*itemAweight + itemBunfilteredTimestamp*itemBweight;

  //============== Write interpolated results into the bufferItem ==================

  bufferItem->DeepCopy(&itemA);
  bufferItem->SetMatrix(interpolatedMatrix); 
  bufferItem->SetFilteredTimestamp(time);
  bufferItem->SetUnfilteredTimestamp(interpolatedUnfilteredTimestamp); 

  return ITEM_OK; 
}

//----------------------------------------------------------------------------
void vtkTrackerBuffer::Clear()
{
  this->TrackerBuffer->Clear();  
}

//----------------------------------------------------------------------------
void vtkTrackerBuffer::SetLocalTimeOffset(double offset)
{
  this->TrackerBuffer->SetLocalTimeOffset(offset); 
}

//----------------------------------------------------------------------------
double vtkTrackerBuffer::GetLocalTimeOffset()
{
  return this->TrackerBuffer->GetLocalTimeOffset(); 
}
