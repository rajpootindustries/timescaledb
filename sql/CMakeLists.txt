set(EXT_SQL_FILE ${PROJECT_NAME}--${PROJECT_VERSION_MOD}.sql)
set(EXT_SQL_UPDATE_FILE ${PROJECT_NAME}--${UPDATE_FROM_VERSION}--${PROJECT_VERSION_MOD}.sql)
set(EXT_SQL_UPDATE_PRE_FILE updates/pre-${UPDATE_FROM_VERSION}--${PROJECT_VERSION_MOD}.sql)
set(EXT_SQL_UPDATE_POST_FILE updates/post-${UPDATE_FROM_VERSION}--${PROJECT_VERSION_MOD}.sql)

set(SQL_FILES
  schemas.sql
  tables.sql
  permissions.sql
  chunk.sql
  ddl_internal.sql
  util_time.sql
  util_internal_table_ddl.sql
  chunk_constraint.sql
  partitioning.sql
  schema_info.sql
  ddl_api.sql
  ddl_triggers.sql
  bookend.sql
  time_bucket.sql
  version.sql
  cache_functions.sql
  size_utils.sql
  histogram.sql
  cache.sql)

set(EXT_SQL_EXTRA_FILES
  timescaledb--0.1.0--0.2.0.sql
  timescaledb--0.2.0--0.3.0.sql
  timescaledb--0.3.0--0.4.0.sql
  timescaledb--0.4.0--0.4.1.sql
  timescaledb--0.4.1--0.4.2.sql
  timescaledb--0.4.2--0.5.0.sql
  timescaledb--0.5.0--0.6.0.sql
  timescaledb--0.6.0--0.6.1.sql
  timescaledb--0.6.1--0.7.0.sql
  timescaledb--0.6.1--0.7.1.sql
  timescaledb--0.7.0--0.7.1.sql
  timescaledb--0.7.1--0.8.0.sql)

set(EXT_SQL_UPDATE_PRE_GLOBAL_FILE updates/pre-global.sql)
set(EXT_SQL_UPDATE_POST_GLOBAL_FILE updates/post-global.sql)
set(UPDATE_FILE_LIST ${EXT_SQL_UPDATE_PRE_GLOBAL_FILE} ${EXT_SQL_UPDATE_PRE_FILE} ${SQL_FILES} ${EXT_SQL_UPDATE_POST_FILE}  ${EXT_SQL_UPDATE_POST_GLOBAL_FILE})

set(MODULE_PATHNAME "$libdir/timescaledb-${PROJECT_VERSION_MOD}")
set(UPDATE_FILE_LIST_VERSIONED "")
set(SQL_FILES_VERSIONED "")
FOREACH(update_file ${UPDATE_FILE_LIST})
  set(update_file_versioned ${update_file}.${PROJECT_VERSION_MOD})
  configure_file(${update_file} ${update_file_versioned} @ONLY)
  list(APPEND UPDATE_FILE_LIST_VERSIONED ${CMAKE_CURRENT_BINARY_DIR}/${update_file_versioned})
  if (${update_file} IN_LIST SQL_FILES)
    list(APPEND SQL_FILES_VERSIONED ${CMAKE_CURRENT_BINARY_DIR}/${update_file_versioned})
  endif()
ENDFOREACH(update_file)

if (WIN32)
  # Make list of files into string of files separated by "+"
  # to make Windows copy concatenate them
  string(REPLACE ";" ";+" SQL_FILES_JOINED "${SQL_FILES_VERSIONED}")
  string(REPLACE ";" ";+" UPDATE_FILES_JOINED "${UPDATE_FILE_LIST_VERSIONED}")
  # Windows copy command requires backslashes for relative paths
  string(REPLACE "/" "\\" EXT_SQL_UPDATE_PRE_FILE "${EXT_SQL_UPDATE_PRE_FILE}")
  string(REPLACE "/" "\\" EXT_SQL_UPDATE_POST_FILE "${EXT_SQL_UPDATE_POST_FILE}")

  set(CAT_SQL_FILE_CMD copy /B /y ${SQL_FILES_JOINED} "\"${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_FILE}\"" >NUL)
  set(CAT_SQL_UPDATE_FILE_CMD copy /B /y ${UPDATE_FILES_JOINED} "\"${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_UPDATE_FILE}\"" >NUL)
else ()
  set(CAT_SQL_FILE_CMD cat ${SQL_FILES_VERSIONED} > ${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_FILE})
  set(CAT_SQL_UPDATE_FILE_CMD cat ${UPDATE_FILE_LIST_VERSIONED} > ${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_UPDATE_FILE})
endif ()

# Command and target for the SQL file
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_FILE}
  DEPENDS ${SQL_FILES}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ${CAT_SQL_FILE_CMD}
  COMMENT "Generating ${EXT_SQL_FILE}")


add_custom_target(sqlfile ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_FILE})

# Command and target for the update SQL file
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_UPDATE_FILE}
  DEPENDS ${UPDATE_FILE_LIST}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMAND ${CAT_SQL_UPDATE_FILE_CMD}
  COMMENT "Generating ${EXT_SQL_UPDATE_FILE}")

add_custom_target(sqlupdatefile ALL DEPENDS ${EXT_SQL_UPDATE_FILE})

install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_FILE}
  ${CMAKE_CURRENT_BINARY_DIR}/${EXT_SQL_UPDATE_FILE}
  ${EXT_SQL_EXTRA_FILES}
  DESTINATION "${PG_SHAREDIR}/extension")
