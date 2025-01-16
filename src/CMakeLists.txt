find_package(Git QUIET)

execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE GIT_BRANCH
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE GIT_COMMIT
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE GIT_TAG
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(
  COMMAND bash -c "date" OUTPUT_VARIABLE BUILD_DATE OUTPUT_STRIP_TRAILING_WHITESPACE
)

# sources
set(HH_DPLANE_SRC
    hh_dp_plugin.c
    hh_dp_process.c
    hh_dp_comm.c
    hh_dp_msg.c
    hh_dp_msg_cache.c
    hh_dp_utils.c
)

add_library(hh_dplane SHARED ${HH_DPLANE_SRC})

set(default_build_type "Release")
set(VERBOSE false)
set(CMAKE_VERBOSE_MAKEFILE true)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -ggdb ${BUILD_FLAGS} -Wno-missing-declarations")
   set(DEBUG_BUILD true)
else()
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb ${BUILD_FLAGS} -Wno-missing-declarations")
   set(DEBUG_BUILD false)
endif()
message("debug " ${DEBUG_BUILD})
message("cflags " ${CMAKE_C_FLAGS})


configure_file (${PROJECT_SOURCE_DIR}/src/hh_dp_config.h.in ${PROJECT_BINARY_DIR}/hh_dp_config.h ESCAPE_QUOTES @ONLY NEWLINE_STYLE UNIX)
target_include_directories(hh_dplane PRIVATE "${PROJECT_BINARY_DIR}")
target_include_directories(hh_dplane PUBLIC /src  PUBLIC /src/lib PUBLIC /usr/local/include/frr)
target_include_directories(hh_dplane PUBLIC "${PROJECT_BINARY_DIR}")
target_link_libraries(hh_dplane frr dplane-rpc)

set_target_properties(hh_dplane PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(hh_dplane PROPERTIES PREFIX "zebra_")
install(TARGETS hh_dplane DESTINATION /usr/local/lib/frr/modules)
