FUNCTION(AddPlusQt5Executable _exec_name)
  LIST(POP_FRONT ARGV)
  ADD_EXECUTABLE(${_exec_name} ${ARGV})

  IF(TARGET Qt5::windeployqt)
      # execute windeployqt in a tmp directory after build
      ADD_CUSTOM_COMMAND(TARGET ${_exec_name}
          POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/windeployqt"
          COMMAND set PATH=%PATH%$<SEMICOLON>${qt5_install_prefix}/bin
          COMMAND Qt5::windeployqt --dir "${CMAKE_CURRENT_BINARY_DIR}/windeployqt" "$<TARGET_FILE_DIR:${_exec_name}>/$<TARGET_FILE_NAME:${_exec_name}>"
          COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/windeployqt $<TARGET_FILE_DIR:${_exec_name}>
      )

      # copy deployment directory during installation
      INSTALL(
          DIRECTORY
          "${CMAKE_CURRENT_BINARY_DIR}/windeployqt/"
          DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
          COMPONENT RuntimeLibraries
      )
  ENDIF()
ENDFUNCTION()
