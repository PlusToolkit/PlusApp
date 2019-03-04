# --------------------------------------------------------------------------
# Install
#
IF(BUILD_DOCUMENTATION)
  IF(WIN32)
    SET(COMPRESSED_HELP_TARGET_FILE_EXT ".chm")
  ELSE()
    SET(COMPRESSED_HELP_TARGET_FILE_EXT ".tar.gz")
  ENDIF()
  INSTALL(FILES
    ${PLUS_EXECUTABLE_OUTPUT_PATH}/Doc/PlusApp-UserManual${COMPRESSED_HELP_TARGET_FILE_EXT}
    DESTINATION ${PLUSAPP_INSTALL_DOCUMENTATION_DIR}
    COMPONENT Documentation
    )
ENDIF()

IF(PLUSBUILD_DOWNLOAD_PLUSLIBDATA AND EXISTS "${PLUSLIB_DATA_DIR}")
  SET(PLUSLIB_CONFIG_FILES
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Sim_PivotCalibration.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Sim_RecordPhantomLandmarks.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Sim_SpatialCalibration_2.0.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Sim_TemporalCalibration.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Sim_VolumeReconstruction.xml

    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_ChRobotics.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Microchip.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Sim_NwirePhantom.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_SimulatedUltrasound_3DSlicer.xml
    ${PLUSLIB_DATA_DIR}/ConfigFiles/SimulatedUltrasound_GelBlockModel_Reference.stl
    ${PLUSLIB_DATA_DIR}/ConfigFiles/SimulatedUltrasound_NeedleModel_NeedleTip.stl
    ${PLUSLIB_DATA_DIR}/ConfigFiles/SimulatedUltrasound_VesselModel_Reference.stl
    ${PLUSLIB_DATA_DIR}/ConfigFiles/SimulatedUltrasound_Scene.mrb

    ${PLUSLIB_DATA_DIR}/CADModels/fCalPhantom/fCal_1.0.stl
    ${PLUSLIB_DATA_DIR}/CADModels/fCalPhantom/fCal_1.2.stl
    ${PLUSLIB_DATA_DIR}/CADModels/fCalPhantom/fCal_2.0.stl
    ${PLUSLIB_DATA_DIR}/CADModels/fCalPhantom/fCal_3.1.stl
    ${PLUSLIB_DATA_DIR}/CADModels/fCalPhantom/fCal_L1.4.stl
    ${PLUSLIB_DATA_DIR}/CADModels/LinearProbe/Probe_L14-5_38.stl
    ${PLUSLIB_DATA_DIR}/CADModels/EndocavityProbe/Probe_EC9-5_10.stl
    ${PLUSLIB_DATA_DIR}/CADModels/CurvilinearProbe/Probe_C5-2_60.stl
    ${PLUSLIB_DATA_DIR}/CADModels/Stylus/Stylus_Example.stl
    )

  IF(PLUS_USE_3dConnexion_TRACKER)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_3dConnexion.xml)
  ENDIF()

  IF(PLUS_USE_Ascension3DG)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Ascension3DG.xml)
  ENDIF()

  IF(PLUS_USE_ATRACSYS)
    LIST(APPEND PLUSLIB_CONFIG_FILES
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Atracsys.xml
      )
    SET(ATRACSYS_MARKERS
      ${PLUSLIB_DATA_DIR}/ConfigFiles/AtracsysTools/stylus.ini
      )
  ENDIF()
  
  IF(PLUS_USE_BKPROFOCUS_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_BkProFocus_OpenIGTLinkTracker.xml
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_BkProFocusOem.xml
      )
    SET(BK_SETTINGS
      ${PLUSLIB_DATA_DIR}/ConfigFiles/BkSettings/IniFile.ccf
      ${PLUSLIB_DATA_DIR}/ConfigFiles/BkSettings/IniFile.ini
      )
  ENDIF()

  IF(PLUS_USE_IntuitiveDaVinci)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/daVinci/daVinci.xml)
  ENDIF()

  IF(PLUS_USE_EPIPHAN)
    LIST(APPEND PLUSLIB_CONFIG_FILES
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_EpiphanVideoCapture.xml
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_EpiphanColorVideoCapture.xml
      )
    IF(PLUS_USE_NDI)
      LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Epiphan_NDIPolaris.xml)
    ENDIF()
  ENDIF()

  IF(PLUS_USE_ICCAPTURING_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_ImagingControlsVideoCapture.xml)
  ENDIF()

  IF(PLUS_USE_INTERSON_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_IntersonVideoCapture.xml)
  ENDIF()

  IF(PLUS_USE_MICRONTRACKER)
    LIST(APPEND PLUSLIB_CONFIG_FILES
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_MicronTracker.xml
      ${PLUSLIB_DATA_DIR}/ConfigFiles/MicronTracker.ini
      )
    SET(MicronTracker_TOOL_DEFINITIONS
      ${PLUSLIB_DATA_DIR}/ConfigFiles/Markers/1b
      ${PLUSLIB_DATA_DIR}/ConfigFiles/Markers/2b
      ${PLUSLIB_DATA_DIR}/ConfigFiles/Markers/a
      ${PLUSLIB_DATA_DIR}/ConfigFiles/Markers/COOLCARD
      ${PLUSLIB_DATA_DIR}/ConfigFiles/Markers/TTblock
      )
    IF(PLUS_USE_ULTRASONIX_VIDEO)
      LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Ultrasonix_L14-5_MicronTracker_2.0.xml)
    ENDIF()
  ENDIF()

  IF(PLUS_USE_MMF_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_MmfVideoCapture.xml
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_MmfColorVideoCapture.xml
      )
  ENDIF()

  IF(PLUS_USE_OPTICAL_MARKER_TRACKER)
    SET(OpticalMarkerTracker_TOOL_DEFINITIONS
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/aruco_calibration_board_a4.pdf
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/realsense_gen2_calibration.yml
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/OpticalMarkerTracker_Scene.mrb
      )
    SET(OpticalMarkerTracker_MARKERS
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00000.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00001.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00002.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00003.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00004.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00005.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00006.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00007.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00008.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00009.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00010.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00011.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00012.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00013.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00014.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00015.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00016.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00017.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00018.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00019.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00020.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00021.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00022.png
      ${PLUSLIB_DATA_DIR}/ConfigFiles/OpticalMarkerTracker/markers/aruco_mip_36h12_00023.png
      )
    IF(PLUS_USE_MMF_VIDEO)
      LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_OpticalMarkerTracker_Mmf.xml)
    ENDIF()
  ENDIF()

  IF(PLUS_USE_OPTITRACK)
    IF (PLUS_MOTIVE_VERSION_MAJOR LESS 2)
      LIST(APPEND PLUSLIB_CONFIG_FILES
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_OptiTrack_TTPandTRA.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_OptiTrack_TTPOnly.xml)
    ELSE()
      SET(OPTITRACK_ADDITIONAL_FILES
        ${PLUSLIB_DATA_DIR}/ConfigFiles/OptiTrack/EmptyProfile.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/OptiTrack/Reference.tra
        ${PLUSLIB_DATA_DIR}/ConfigFiles/OptiTrack/SampleProfile.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/OptiTrack/Stylus.tra
        )
      LIST(APPEND PLUSLIB_CONFIG_FILES
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_OptiTrack_AddMarkersUsingTRA.xml 
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_OptiTrack_Profile.xml 
        ${OPTITRACK_ADDITIONAL_FILES}
        )
    ENDIF()
  ENDIF()

  IF(PLUS_USE_PHIDGET_SPATIAL_TRACKER)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_PhidgetSpatial.xml)
  ENDIF()

  IF(PLUS_USE_PHILIPS_3D_ULTRASOUND)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Philips_ie33_NDIAurora.xml)
  ENDIF()

  IF(PLUS_USE_NDI)
    LIST(APPEND PLUSLIB_CONFIG_FILES
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Ultrasonix_L14-5_NDIPolaris_2.0.xml
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_NDIPolaris.xml
      ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_NDIAurora.xml
      )
    SET(NDI_TOOL_DEFINITIONS
      ${PLUSLIB_DATA_DIR}/ConfigFiles/NdiToolDefinitions/8700339.rom
      ${PLUSLIB_DATA_DIR}/ConfigFiles/NdiToolDefinitions/8700340.rom
      ${PLUSLIB_DATA_DIR}/ConfigFiles/NdiToolDefinitions/8700449.rom
      )
  ENDIF()

  IF(PLUS_USE_STEALTHLINK)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_StealthLinkTracker.xml)
  ENDIF()

  IF(PLUS_USE_TELEMED_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_TelemedVideoCapture.xml)
  ENDIF()

  IF(PLUS_USE_THORLABS_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_ThorLabsVideoCapture.xml)
  ENDIF()

  IF(PLUS_USE_ULTRASONIX_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Ultrasonix_L14-5.xml)

    IF(PLUS_USE_Ascension3DG)
      LIST(APPEND PLUSLIB_CONFIG_FILES
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Ultrasonix_L14-5_Ascension3DG_2.0.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Ultrasonix_L14-5_Ascension3DG_3.0.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_fCal_Ultrasonix_L14-5_Ascension3DG_L1.4.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Ultrasonix_L14-5_Ascension3DG_calibrated.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Ultrasonix_C5-2_Ascension3DG_calibrated.xml
        ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_Ultrasonix_4DL14-5_Porta_calibrated.xml
        )
    ENDIF()
  ENDIF()

  IF(PLUS_USE_VFW_VIDEO)
    LIST(APPEND PLUSLIB_CONFIG_FILES ${PLUSLIB_DATA_DIR}/ConfigFiles/PlusDeviceSet_Server_VfwVideoCapture.xml)
  ENDIF()

  SET(PLUSLIB_DATA_FILES
    ${PLUSLIB_DATA_DIR}/TestImages/fCal_Test_Calibration_3NWires.igs.mha
    ${PLUSLIB_DATA_DIR}/TestImages/fCal_Test_Calibration_3NWires_fCal2.0.igs.mha
    ${PLUSLIB_DATA_DIR}/TestImages/WaterTankBottomTranslationTrackerBuffer-trimmed.igs.mha
    ${PLUSLIB_DATA_DIR}/TestImages/WaterTankBottomTranslationVideoBuffer.igs.mha
    ${PLUSLIB_DATA_DIR}/TestImages/EightLandmarkPointsTrackedForPhantomRegistration.igs.mha
    )
ENDIF()

list(REMOVE_DUPLICATES PLUSAPP_QT_COMPONENTS)
FOREACH(_qt_component ${PLUSAPP_QT_COMPONENTS})
  IF(TARGET Qt5::${_qt_component})
    GET_TARGET_PROPERTY(_configs Qt5::${_qt_component} IMPORTED_CONFIGURATIONS)
    FOREACH(_config IN LISTS _configs)
    GET_TARGET_PROPERTY(_location Qt5::${_qt_component} IMPORTED_LOCATION_${_config})
      IF(${_config} STREQUAL RELEASE OR ${_config} STREQUAL NOCONFIG)
        LIST(APPEND _entries ${_config})
        LIST(APPEND PlusApp_QT_INSTALL_FILES ${_location})
        BREAK()
      ENDIF()
    ENDFOREACH()
  ENDIF()

  LIST(LENGTH _entries _size)
  IF(_size EQUAL 0)
    MESSAGE(FATAL_ERROR "Unable to locate Qt5::${_qt_component} library file for install.")
  ENDIF()
ENDFOREACH()

INSTALL(FILES ${PlusApp_QT_INSTALL_FILES}
  DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
  COMPONENT RuntimeLibraries
  )
IF(WIN32)
  INSTALL(FILES ${QT_ROOT_DIR}/plugins/platforms/qwindows${CMAKE_SHARED_LIBRARY_SUFFIX}
    DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}/platforms
    COMPONENT RuntimeLibraries
    )
  # Install Plus command prompt starting script
  INSTALL(FILES
      ${PLUSLIB_SOURCE_DIR}/src/scripts/StartPlusCommandPrompt.bat
      ${PLUSLIB_SOURCE_DIR}/src/scripts/StartPlusCommandPrompt.ico
    DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
    COMPONENT Scripts
   )
ELSEIF(UNIX AND NOT APPLE)
  INSTALL(FILES ${QT_ROOT_DIR}/plugins/platforms/libqxcb${CMAKE_SHARED_LIBRARY_SUFFIX}
    DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}/platforms
    COMPONENT RuntimeLibraries
    )
ENDIF()

IF(PLUSAPP_INSTALL_CONFIG_DIR)
  INSTALL(FILES ${PLUSLIB_CONFIG_FILES}
    DESTINATION ${PLUSAPP_INSTALL_CONFIG_DIR}
    COMPONENT Data
    )
  IF(PLUS_USE_NDI)
    INSTALL(FILES ${NDI_TOOL_DEFINITIONS}
      DESTINATION ${PLUSAPP_INSTALL_CONFIG_DIR}/NdiToolDefinitions
      COMPONENT Data
      )
  ENDIF()
  IF(PLUS_USE_ATRACSYS)
    INSTALL(FILES ${ATRACSYS_MARKERS}
      DESTINATION ${PLUSAPP_INSTALL_CONFIG_DIR}/AtracsysTools
      COMPONENT Data
      )
  ENDIF()
  IF(PLUS_USE_OPTICAL_MARKER_TRACKER)
    INSTALL(FILES ${OpticalMarkerTracker_TOOL_DEFINITIONS}
      DESTINATION ${PLUSAPP_INSTALL_CONFIG_DIR}/OpticalMarkerTracker
      COMPONENT Data
      )
    INSTALL(FILES ${OpticalMarkerTracker_MARKERS}
      DESTINATION ${PLUSAPP_INSTALL_CONFIG_DIR}/OpticalMarkerTracker/markers
      COMPONENT Data
      )
  ENDIF()

  IF(PLUS_USE_BKPROFOCUS_VIDEO)
    INSTALL(FILES ${BK_SETTINGS}
      DESTINATION ${PLUSAPP_INSTALL_CONFIG_DIR}/BkSettings
      COMPONENT Data
      )
  ENDIF()
  IF(PLUS_USE_MICRONTRACKER)
    INSTALL(FILES ${MicronTracker_TOOL_DEFINITIONS}
      DESTINATION ${PLUSAPP_INSTALL_CONFIG_DIR}/Markers
      COMPONENT Data
      )
  ENDIF()
ENDIF()

IF(PLUSAPP_INSTALL_DATA_DIR)
  INSTALL(FILES ${PLUSLIB_DATA_FILES}
    DESTINATION ${PLUSAPP_INSTALL_DATA_DIR}
    COMPONENT Data
    )
ENDIF()

INSTALL(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/PlusConfig.xml
  DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
  COMPONENT Data
  )
