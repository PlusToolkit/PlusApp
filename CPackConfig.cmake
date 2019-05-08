IF("${PLUSAPP_PACKAGE_EDITION}" STREQUAL "")
  SET(PLUSAPP_PACKAGE_EDITION_PLATFORM "${PLUSLIB_PLATFORM}")
ELSE()
  SET(PLUSAPP_PACKAGE_EDITION_PLATFORM "${PLUSAPP_PACKAGE_EDITION}-${PLUSLIB_PLATFORM}")
ENDIF()

SET(PLUS_COMMIT_DATE ${PLUSAPP_COMMIT_DATE})
SET(PLUS_COMMIT_DATE_NO_DASHES ${PLUSAPP_COMMIT_DATE_NO_DASHES})
IF(PLUSLIB_COMMIT_DATE STRGREATER PLUSAPP_COMMIT_DATE)
  SET(PLUS_COMMIT_DATE ${PLUSLIB_COMMIT_DATE})
  SET(PLUS_COMMIT_DATE_NO_DASHES ${PLUSLIB_COMMIT_DATE_NO_DASHES})
ENDIF()

SET(CPACK_GENERATOR "ZIP")
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Plus(Public software Library for UltraSound) for ${PLUSAPP_PACKAGE_EDITION_PLATFORM}")
SET(CPACK_PACKAGE_VENDOR "PerkLab, Queen's University")
# SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ReadMe.txt")
IF(EXISTS ${PLUSLIB_SOURCE_DIR}/License.txt)
  SET(_license_file ${PLUSLIB_SOURCE_DIR}/License.txt)
ELSE()
  SET(_license_file ${PLUSLIB_SOURCE_DIR}/src/License.txt)
ENDIF()
SET(CPACK_RESOURCE_FILE_LICENSE "${_license_file}")
SET(CPACK_PACKAGE_VERSION_MAJOR ${PLUSAPP_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${PLUSAPP_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${PLUSAPP_VERSION_PATCH})
SET(CPACK_PACKAGE_FILE_NAME "PlusApp-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}.${PLUS_COMMIT_DATE_NO_DASHES}-${PLUSAPP_PACKAGE_EDITION_PLATFORM}" )
SET(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_FILE_NAME}")
SET(CPACK_INSTALL_CMAKE_PROJECTS "${PlusApp_BINARY_DIR};PlusApp;ALL;/")
SET(CPACK_PACKAGE_EXECUTABLES
  "PlusServerLauncher" "Plus Server Launcher"
  "fCal" "Free-hand calibration(fCal)"
  )

IF(WIN32)
  LIST(APPEND CPACK_GENERATOR "NSIS")
  SET(CPACK_NSIS_PACKAGE_NAME "Plus Applications ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}.${PLUS_COMMIT_DATE} (${PLUSAPP_PACKAGE_EDITION_PLATFORM})" )

  # Install into c:\Users\<username>\PlusApp_...
  SET(CPACK_NSIS_INSTALL_ROOT "$PROFILE")

  # Do not ask for admin access rights(no UAC dialog), to allow installation without admin access
  SET(CPACK_NSIS_DEFINES ${CPACK_NSIS_DEFINES} "RequestExecutionLevel user")

  SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS)
  SET(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS)

  # Windows users may not be familiar how to open a command prompt, so create a shortcut for that
  LIST(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Plus command prompt.lnk\\\" \\\"$INSTDIR\\\\bin\\\\StartPlusCommandPrompt.bat\\\" \\\"$INSTDIR\\\\bin\\\\StartPlusCommandPrompt.bat\\\" \\\"$INSTDIR\\\\bin\\\\StartPlusCommandPrompt.ico\\\"
    ")
  LIST(APPEND CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    !insertmacro MUI_STARTMENU_GETFOLDER Application $MUI_TEMP
    Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Plus command prompt.lnk\\\"
    ")
  IF(BUILD_DOCUMENTATION)
    # Create a shortcut to documentation as well
    LIST(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
      CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Plus user manual.lnk\\\" \\\"$INSTDIR\\\\doc\\\\PlusApp-UserManual.chm\\\"
      ")
    LIST(APPEND CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
      !insertmacro MUI_STARTMENU_GETFOLDER Application $MUI_TEMP
      Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Plus user manual.lnk\\\"
      ")
  ENDIF()
ELSEIF(UNIX AND NOT APPLE)
  LIST(APPEND CPACK_GENERATOR "DEB")
  SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Laboratory for Percutaneous Surgery")
  SET(CPACK_DEBIAN_PACKAGE_NAME "plusapp-${PLUSAPP_PACKAGE_EDITION_PLATFORM}") # naming rules here https://www.debian.org/doc/debian-policy/ch-controlfields.html#s-f-Source
  SET(CPACK_DEBIAN_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
  SET(CPACK_DEBIAN_PACKAGE_RELEASE "${PLUSAPP_SHORT_REVISION}")
  SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
  SET(CPACK_DEBIAN_COMPRESSION_TYPE gzip)
  SET(CPACK_DEBIAN_PACKAGE_SECTION science)
  SET(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
  SET(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://plustoolkit.github.io/")
ENDIF()

IF(EXISTS "${PlusLib_DIR}/CMakeCache.txt")
  LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${PlusLib_DIR};PlusLib;RuntimeExecutables;/")
  LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${PlusLib_DIR};PlusLib;RuntimeLibraries;/")
  LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${PlusLib_DIR};PlusLib;Scripts;/")
ELSE()
  MESSAGE(WARNING "Unable to set PlusLib_DIR for package generation!")
ENDIF()

IF(EXISTS "${VTK_DIR}/VTKTargets.cmake")
  LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${VTK_DIR};VTK;RuntimeLibraries;/")
ELSE()
  MESSAGE(WARNING "Unable to set VTK_DIR for package generation!")
ENDIF()

IF(EXISTS "${ITK_DIR}/ITKTargets.cmake")
  SET(ITK_USE_REVIEW OFF)
  LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${ITK_DIR};ITK;RuntimeLibraries;/")
ELSE()
  MESSAGE(WARNING "Unable to set ITK_DIR for package generation!")
ENDIF()

IF(PLUS_USE_OpenIGTLink)
  IF(EXISTS "${OpenIGTLink_DIR}/CMakeCache.txt")
    LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${OpenIGTLink_DIR};OpenIGTLink;RuntimeLibraries;/")
  ELSE()
    MESSAGE(WARNING "Unable to set OpenIGTLink_DIR for package generation!")
  ENDIF()

  IF(EXISTS "${OpenIGTLinkIO_DIR}/CMakeCache.txt")
    LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${OpenIGTLinkIO_DIR};OpenIGTLinkIO;RuntimeLibraries;/")
  ELSE()
    MESSAGE(WARNING "Unable to set OpenIGTLinkIO_DIR for package generation!")
  ENDIF()
ENDIF()

SET(_opencv_added OFF)
IF(PLUS_USE_OpenCV)
  IF(EXISTS "${OpenCV_DIR}/CMakeCache.txt")
    LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${OpenCV_DIR};OpenCV;libs;/")
    SET(_opencv_added ON)
  ELSE()
    MESSAGE(WARNING "Unable to set OpenCV_DIR for package generation!")
  ENDIF()
ENDIF()

# OpenCV uses OpenMP. Set CMAKE_INSTALL_OPENMP_LIBRARIES flag to make
# InstallRequiredSystemLibraries install OpenMP libraries.
# Without this, Plus may fail to start due to missing vcomp120.dll.
SET(CMAKE_INSTALL_OPENMP_LIBRARIES ${PLUS_USE_OpenCV})

IF(PLUS_USE_OvrvisionPro)
  IF(EXISTS "${OvrvisionPro_DIR}/CMakeCache.txt")
    LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${OvrvisionPro_DIR};OvrvisionPro;RuntimeLibraries;/")
  ELSE()
    MESSAGE(WARNING "Unable to set OvrvisionPro_DIR for package generation!")
  ENDIF()

  IF(NOT _opencv_added AND EXISTS "${OpenCV_DIR}/CMakeCache.txt")
    LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${OpenCV_DIR};OpenCV;libs;/")
  ELSEIF(NOT _opencv_added)
    MESSAGE(WARNING "Unable to set OpenCV_DIR for package generation!")
  ENDIF()
ENDIF()

IF(PLUS_USE_aruco)
  IF(EXISTS "${aruco_DIR}/CMakeCache.txt")
    LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${aruco_DIR};aruco;ALL;/")
  ELSE()
    MESSAGE(WARNING "Unable to set aruco_DIR for package generation!")
  ENDIF()
ENDIF()

IF(EXISTS "${vtkAddon_DIR}/CMakeCache.txt")
  LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${vtkAddon_DIR};vtkAddon;RuntimeLibraries;/")
ELSE()
  MESSAGE(WARNING "Unable to set vtkAddon_DIR for package generation!")
ENDIF()

IF(EXISTS "${IGSIO_DIR}/CMakeCache.txt")
  LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${IGSIO_DIR};IGSIO;RuntimeLibraries;/")
ELSE()
  MESSAGE(WARNING "Unable to set IGSIO_DIR for package generation!")
ENDIF()

#-----------------------------------------------------------------------------
# Installation vars.
# PLUSAPP_INSTALL_BIN_DIR          - binary dir(executables)
# PLUSAPP_INSTALL_LIB_DIR          - library dir(libs)
# PLUSAPP_INSTALL_DATA_DIR         - share dir(say, examples, data, etc)
# PLUSAPP_INSTALL_CONFIG_DIR       - config dir(configuration files)
# PLUSAPP_INSTALL_SCRIPTS_DIR      - scripts dir
# PLUSAPP_INSTALL_INCLUDE_DIR      - include dir(headers)
# PLUSAPP_INSTALL_PACKAGE_DIR      - package/export configuration files
# PLUSAPP_INSTALL_NO_DEVELOPMENT   - do not install development files
# PLUSAPP_INSTALL_NO_RUNTIME       - do not install runtime files
# PLUSAPP_INSTALL_NO_DOCUMENTATION - do not install documentation files
# Applications
# RuntimeLibraries
# Development

IF(NOT PLUSAPP_INSTALL_BIN_DIR)
  SET(PLUSAPP_INSTALL_BIN_DIR "bin")
ENDIF()

IF(NOT PLUSAPP_INSTALL_LIB_DIR)
  SET(PLUSAPP_INSTALL_LIB_DIR "lib")
ENDIF()

IF(NOT PLUSAPP_INSTALL_DATA_DIR)
  SET(PLUSAPP_INSTALL_DATA_DIR "data")
ENDIF()

IF(NOT PLUSAPP_INSTALL_CONFIG_DIR)
  SET(PLUSAPP_INSTALL_CONFIG_DIR "config")
ENDIF()

IF(NOT PLUSAPP_INSTALL_SCRIPTS_DIR)
  SET(PLUSAPP_INSTALL_SCRIPTS_DIR "scripts")
ENDIF()

IF(NOT PLUSAPP_INSTALL_INCLUDE_DIR)
  SET(PLUSAPP_INSTALL_INCLUDE_DIR "include")
ENDIF()

IF(NOT PLUSAPP_INSTALL_DOCUMENTATION_DIR)
  SET(PLUSAPP_INSTALL_DOCUMENTATION_DIR "doc")
ENDIF()

IF(NOT PLUSAPP_INSTALL_NO_DOCUMENTATION)
  SET(PLUSAPP_INSTALL_NO_DOCUMENTATION 0)
ENDIF()

INCLUDE(InstallRequiredSystemLibraries)
INCLUDE(CPack)
