if(NOT GIT)
find_package(Git QUIET)
endif()

if(NOT GIT_BRANCH)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE GIT_BRANCH
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(NOT GIT_COMMIT)
execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE GIT_COMMIT
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(NOT GIT_TAG)
execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	OUTPUT_VARIABLE GIT_TAG
	ERROR_QUIET
	OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

if(NOT BUILD_DATE)
execute_process(
  COMMAND bash -c "date" OUTPUT_VARIABLE BUILD_DATE OUTPUT_STRIP_TRAILING_WHITESPACE
)
endif()

if(NOT OUT)
  set(OUT "/usr/local")
endif()

if(NOT HH_FRR_SRC)
  set(HH_FRR_SRC "/src")
endif()

if(NOT HH_FRR_INCLUDE)
  set(HH_FRR_INCLUDE "/usr/local/include/frr")
endif()


# sources
set(HH_DPLANE_SRC
    hh_dp_plugin.c
    hh_dp_process.c
    hh_dp_comm.c
    hh_dp_msg.c
    hh_dp_msg_cache.c
    hh_dp_utils.c
    hh_dp_rpc_stats.c
    hh_dp_vty.c
)

add_library(hh_dplane SHARED ${HH_DPLANE_SRC})

set(default_build_type "Release")
set(VERBOSE false)
set(CMAKE_VERBOSE_MAKEFILE true)

if(CMAKE_BUILD_TYPE STREQUAL Debug)
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-declarations")
   set(DEBUG_BUILD true)
else()
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-declarations")
   set(DEBUG_BUILD false)
endif()

set(CMAKE_C_STANDARD "23")

message("debug " ${DEBUG_BUILD})
message("cflags " ${CMAKE_C_FLAGS})


configure_file (${PROJECT_SOURCE_DIR}/src/hh_dp_config.h.in ${PROJECT_BINARY_DIR}/hh_dp_config.h ESCAPE_QUOTES @ONLY NEWLINE_STYLE UNIX)
target_include_directories(hh_dplane PRIVATE "${PROJECT_BINARY_DIR}")
target_include_directories(hh_dplane PUBLIC ${HH_FRR_SRC} PUBLIC ${HH_FRR_SRC}/lib PUBLIC ${HH_FRR_INCLUDE})
target_include_directories(hh_dplane PUBLIC "${PROJECT_BINARY_DIR}")
target_link_libraries(hh_dplane frr dplane-rpc)

set_target_properties(hh_dplane PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(hh_dplane PROPERTIES PREFIX "zebra_")
install(TARGETS hh_dplane DESTINATION ${OUT}/lib/frr/modules)

# vtysh extension lib
add_library(vtysh_ext SHARED vtysh_hhdplane.c)
set_target_properties(vtysh_ext PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vtysh_ext PUBLIC ${HH_FRR_SRC} PUBLIC ${HH_FRR_SRC}/lib PUBLIC ${HH_FRR_INCLUDE})
target_link_libraries(vtysh_ext frr)
set_target_properties(vtysh_ext PROPERTIES PREFIX "")
set_target_properties(vtysh_ext PROPERTIES OUTPUT_NAME "libvtysh_hedgehog")
set_target_properties(vtysh_ext PROPERTIES SUFFIX ".so")
install(TARGETS vtysh_ext DESTINATION ${OUT}/lib)
