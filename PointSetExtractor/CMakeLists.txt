# --------------------------------------------------------------------------
# PointSetExtractor
SET(VTK_RENDERING_LIB "")
IF(PLUS_RENDERING_ENABLED)
  SET(VTK_RENDERING_LIB vtkRendering${VTK_RENDERING_BACKEND})
ENDIF()

ADD_EXECUTABLE(PointSetExtractor PointSetExtractor.cxx)
SET_TARGET_PROPERTIES(PointSetExtractor PROPERTIES FOLDER Utilities)
TARGET_LINK_LIBRARIES(PointSetExtractor PUBLIC 
  vtkPlusCommon
  vtkInteractionStyle
  vtkRenderingFreeType
  vtkFiltersCore
  vtkFiltersSources
  vtkIOPLY
  vtkIOGeometry
  ${VTK_RENDERING_LIB}
  )
GENERATE_HELP_DOC(PointSetExtractor)

# --------------------------------------------------------------------------
# Install
IF(PLUSAPP_INSTALL_BIN_DIR)
  INSTALL(TARGETS PointSetExtractor
    DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
    COMPONENT RuntimeExecutables
    )  
ENDIF()
