
// This test program sends the contents of a saved tracked image sequence and sends it through OpenIGTLink

#include <ctime>
#include <iostream>

#include "PlusConfigure.h"

#include "vtkMatrix4x4.h"
#include "vtkSmartPointer.h"
#include "vtksys/CommandLineArguments.hxx"
#include "vtkXMLUtilities.h"

#include "vtkDataCollector.h"
#include "vtkSavedDataTracker.h"
#include "vtkSavedDataVideoSource.h"

#include "vtkOpenIGTLinkBroadcaster.h"

#include "OpenIGTLinkReceiveServer.h"



enum {
  BC_EXIT_SUCCESS = 0,
  BC_EXIT_FAILURE
};

const double DELAY_BETWEEN_MESSAGES_SEC = 0.18;


int main( int argc, char** argv )
{
  std::string inputConfigFileName;
  std::string inputVideoBufferMetafile;
  std::string inputTrackerBufferMetafile;
  bool inputReplay(false); 
  int verboseLevel=vtkPlusLogger::LOG_LEVEL_INFO;
  
  vtksys::CommandLineArguments args;
  args.Initialize( argc, argv );
  
  args.AddArgument( "--input-config-file", vtksys::CommandLineArguments::EQUAL_ARGUMENT,
                    &inputConfigFileName, "Name of the input configuration file." );
  args.AddArgument( "--input-video-buffer-metafile", vtksys::CommandLineArguments::EQUAL_ARGUMENT,
                    &inputVideoBufferMetafile, "Video buffer sequence metafile." );
  args.AddArgument( "--input-tracker-buffer-metafile", vtksys::CommandLineArguments::EQUAL_ARGUMENT,
                    &inputTrackerBufferMetafile, "Tracker buffer sequence metafile." );
  args.AddArgument( "--replay", vtksys::CommandLineArguments::NO_ARGUMENT,
                    &inputReplay, "Replay tracked frames after reached the latest one." );
  args.AddArgument("--verbose", vtksys::CommandLineArguments::EQUAL_ARGUMENT, 
                      &verboseLevel, "Verbose level (1=error only, 2=warning, 3=info, 4=debug 5=trace)");  

  if ( ! args.Parse() )
  {
    std::cerr << "Problem parsing arguments" << std::endl;
    std::cout << "Help: " << args.GetHelp() << std::endl;
    exit( BC_EXIT_FAILURE );
  }
  
  if ( inputConfigFileName.empty() )
  {
    std::cerr << "input-config-file is required" << std::endl;
    exit( BC_EXIT_FAILURE );
  }
  
  vtkPlusLogger::Instance()->SetLogLevel( verboseLevel );
  vtkPlusLogger::Instance()->SetDisplayLogLevel( verboseLevel );
  
  
  // Prepare data collector object.
  vtkSmartPointer<vtkXMLDataElement> configRootElement = vtkXMLUtilities::ReadElementFromFile(inputConfigFileName.c_str());
  if (configRootElement == NULL)
  {	
    LOG_ERROR("Unable to read configuration from file " << inputConfigFileName.c_str()); 
    return BC_EXIT_FAILURE;
  }

  vtkDataCollector* dataCollector = vtkDataCollector::New();
  dataCollector->ReadConfiguration( configRootElement );
  
  if ( dataCollector->GetAcquisitionType() == SYNCHRO_VIDEO_SAVEDDATASET )
    {
    if ( inputVideoBufferMetafile.empty() )
      {
      LOG_ERROR("Video source metafile missing.");
      return BC_EXIT_FAILURE;
      }

    vtkSavedDataVideoSource* videoSource = dynamic_cast< vtkSavedDataVideoSource* >( dataCollector->GetVideoSource() );
    if ( videoSource == NULL )
      {
      LOG_ERROR("Invalid saved data video source.");
      exit( BC_EXIT_FAILURE );
      }
    videoSource->SetSequenceMetafile( inputVideoBufferMetafile.c_str() );
    videoSource->SetReplayEnabled( inputReplay ); 
    }

  if ( dataCollector->GetTrackerType() == TRACKER_SAVEDDATASET )
    {
    if ( inputTrackerBufferMetafile.empty() )
      {
      LOG_ERROR("Tracker source metafile missing.");
      return BC_EXIT_FAILURE;
      }
    vtkSavedDataTracker* tracker = static_cast< vtkSavedDataTracker* >( dataCollector->GetTracker() );
    tracker->SetSequenceMetafile( inputTrackerBufferMetafile.c_str() );
    tracker->SetReplayEnabled( inputReplay ); 
    tracker->Connect();
    }
  
  LOG_INFO("Initializing data collector... ");
  dataCollector->Initialize();
  
  
  
    // Prepare server to receive messages.
  
  OpenIGTLinkReceiveServer receiveServer( 18944 );
  receiveServer.Start();
  
  
  
    // Prepare the OpenIGTLink broadcaster.
  
  vtkOpenIGTLinkBroadcaster::Status broadcasterStatus = vtkOpenIGTLinkBroadcaster::STATUS_NOT_INITIALIZED;
  vtkSmartPointer< vtkOpenIGTLinkBroadcaster > broadcaster = vtkSmartPointer< vtkOpenIGTLinkBroadcaster >::New();
    broadcaster->SetDataCollector( dataCollector );
  
  std::string errorMessage;
  broadcasterStatus = broadcaster->Initialize( errorMessage );
  switch ( broadcasterStatus )
    {
    case vtkOpenIGTLinkBroadcaster::STATUS_OK:
      // no error, continue
      break;
    case vtkOpenIGTLinkBroadcaster::STATUS_NOT_INITIALIZED:
      LOG_ERROR("Couldn't initialize OpenIGTLink broadcaster." );
      exit( BC_EXIT_FAILURE );
    case vtkOpenIGTLinkBroadcaster::STATUS_HOST_NOT_FOUND:
      LOG_ERROR( "Could not connect to host: " << errorMessage );
      exit( BC_EXIT_FAILURE );
    case vtkOpenIGTLinkBroadcaster::STATUS_MISSING_DEFAULT_TOOL:
      LOG_ERROR( "Default tool not defined. ");
      exit( BC_EXIT_FAILURE );
    default:
      LOG_ERROR( "Unknown error while trying to intialize the broadcaster. " );
      exit( BC_EXIT_FAILURE );
    }

  LOG_INFO("Start data collector... ");
  dataCollector->Start();
  
  
  unsigned int NUMBER_OF_BROADCASTED_MESSAGES = UINT_MAX;
  
  if ( dataCollector->GetAcquisitionType() == SYNCHRO_VIDEO_SAVEDDATASET )
    {
    NUMBER_OF_BROADCASTED_MESSAGES = dataCollector->GetVideoSource()->GetBuffer()->GetBufferSize();
    if ( inputReplay )
      {
      NUMBER_OF_BROADCASTED_MESSAGES = UINT_MAX; 
      }
    }
  
  
  
    // Send messages.
  
  for ( int i = 0; i < NUMBER_OF_BROADCASTED_MESSAGES; ++ i )
    {
    vtkAccurateTimer::Delay( DELAY_BETWEEN_MESSAGES_SEC );
    

    std::ostringstream ss;
    ss.precision( 2 ); 
    
    ss << "Iteration: " << i << std::endl;

    vtkSmartPointer< vtkMatrix4x4 > tFrame2Tracker = vtkSmartPointer< vtkMatrix4x4 >::New(); 
    if ( dataCollector->GetTracker()->IsTracking() )
      {
      double timestamp( 0 ); 
      TrackerStatus status = TR_OK; 
      dataCollector->GetTransformWithTimestamp( tFrame2Tracker, timestamp, status, dataCollector->GetTracker()->GetFirstPortNumberByType(TRACKER_TOOL_PROBE) ); 
      
      ss << "Timestamp: " << timestamp << std::endl;
      
      if ( status == TR_MISSING || status == TR_OUT_OF_VIEW ) 
        {
        ss  << "Tracker out of view..." << std::endl; 
        }
      else if ( status == TR_REQ_TIMEOUT ) 
        {
        ss  << "Tracker request timeout..." << std::endl; 
        }
      else
        {
        ss  << std::fixed 
          << tFrame2Tracker->GetElement(0,0) << "   " << tFrame2Tracker->GetElement(0,1) << "   "
            << tFrame2Tracker->GetElement(0,2) << "   " << tFrame2Tracker->GetElement(0,3) << "\n"
          << tFrame2Tracker->GetElement(1,0) << "   " << tFrame2Tracker->GetElement(1,1) << "   "
            << tFrame2Tracker->GetElement(1,2) << "   " << tFrame2Tracker->GetElement(1,3) << "\n"
          << tFrame2Tracker->GetElement(2,0) << "   " << tFrame2Tracker->GetElement(2,1) << "   "
            << tFrame2Tracker->GetElement(2,2) << "   " << tFrame2Tracker->GetElement(2,3) << "\n"
          << tFrame2Tracker->GetElement(3,0) << "   " << tFrame2Tracker->GetElement(3,1) << "   "
            << tFrame2Tracker->GetElement(3,2) << "   " << tFrame2Tracker->GetElement(3,3) << "\n"; 
        }
      
      }
    else
      {
      ss << "Unable to connect to tracker...";    
      }
    
    LOG_INFO( ss.str() );
    
    vtkOpenIGTLinkBroadcaster::Status broadcasterStatus = vtkOpenIGTLinkBroadcaster::STATUS_NOT_INITIALIZED;
    std::string                       errorMessage;
    
    broadcasterStatus = broadcaster->SendMessages( errorMessage );
    
    
      // Display messages depending on the status of broadcaster.
    
    switch (broadcasterStatus)
      {
      case vtkOpenIGTLinkBroadcaster::STATUS_OK:
        // no error, no message
        break;
      case vtkOpenIGTLinkBroadcaster::STATUS_HOST_NOT_FOUND:
        LOG_WARNING("Host not found: " << errorMessage);
        break;
      case vtkOpenIGTLinkBroadcaster::STATUS_NOT_INITIALIZED:
        LOG_WARNING("OpenIGTLink broadcaster not initialized.");
        break;
      case vtkOpenIGTLinkBroadcaster::STATUS_NOT_TRACKING:
        LOG_WARNING("Tracking error detected.");
        break;
      case vtkOpenIGTLinkBroadcaster::STATUS_SEND_ERROR:
        LOG_WARNING("Could not send OpenIGTLink message.");
        break;
      default:
        LOG_WARNING("Unknown status while trying to send OpenIGTLink message.");
      }
    }
  
  
  receiveServer.Stop();
  int numReceivedMessages = receiveServer.GetNumberOfReceivedMessages();
  LOG_INFO( "Received OpenIGTLink messages: " << numReceivedMessages );
  
  
  LOG_INFO("Stop data collector... ");
  dataCollector->Stop();
  LOG_INFO("Done.");
  
  LOG_INFO("Deleting data collector... ");
  dataCollector->Delete();
  LOG_INFO("Done.");
  
  return BC_EXIT_SUCCESS;
}
