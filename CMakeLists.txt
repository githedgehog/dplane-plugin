cmake_minimum_required(VERSION 3.24)
project(HH-DP-PLUGIN
    VERSION 0.0.1
    DESCRIPTION "HedgeHog Gateway FRR dataplane plugin"
    LANGUAGES C)

find_library(FRR frr)
if(NOT FRR)
  message(FATAL_ERROR "Could not find FRR library")
else()
  message("FRR found")
endif()

add_subdirectory(src)
