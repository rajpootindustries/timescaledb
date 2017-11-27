set(HEADERS
  extension.h
)

set(SOURCES
  extension.c
  init.c)

add_library(${PROJECT_NAME}-loader MODULE ${SOURCES} ${HEADERS})

set_target_properties(${PROJECT_NAME}-loader PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    PREFIX "")

install(
  TARGETS ${PROJECT_NAME}-loader
  DESTINATION ${PG_PKGLIBDIR})


IF(CMAKE_BUILD_TYPE MATCHES Debug)
  add_subdirectory(../../test/loader-mock/ "${CMAKE_CURRENT_BINARY_DIR}/mock")
ENDIF(CMAKE_BUILD_TYPE MATCHES Debug)
