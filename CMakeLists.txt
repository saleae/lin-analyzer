cmake_minimum_required (VERSION 3.11)
project(lin_analyzer)

add_definitions( -DLOGIC2 )

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum supported MacOS version" FORCE)

# enable generation of compile_commands.json, helpful for IDEs to locate include files.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# custom CMake Modules are located in the cmake directory.
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(ExternalAnalyzerSDK)

set(SOURCES 
src/LINAnalyzer.cpp
src/LINAnalyzer.h
src/LINAnalyzerResults.cpp
src/LINAnalyzerResults.h
src/LINAnalyzerSettings.cpp
src/LINAnalyzerSettings.h
src/LINChecksum.cpp
src/LINChecksum.h
src/LINSimulationDataGenerator.cpp
src/LINSimulationDataGenerator.h
)

add_analyzer_plugin(lin_analyzer SOURCES ${SOURCES})
