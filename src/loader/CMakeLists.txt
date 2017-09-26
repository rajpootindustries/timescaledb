set(HEADERS
  extension.h
  load.h)

set(SOURCES
  extension.c
  init.c
  load.c)

add_library(${PROJECT_NAME}-loader MODULE ${SOURCES} ${HEADERS})

set_target_properties(${PROJECT_NAME}-loader PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}
    PREFIX "")

install(
  TARGETS ${PROJECT_NAME}-loader
  DESTINATION ${PG_PKGLIBDIR})
