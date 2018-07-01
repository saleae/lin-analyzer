cmake_minimum_required (VERSION 3.11)
project(lin_analyzer)

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

add_library(lin_analyzer SHARED ${SOURCES})
target_link_libraries(lin_analyzer PRIVATE Saleae::AnalyzerSDK)

if(MSVC)
    install(TARGETS lin_analyzer RUNTIME DESTINATION ".")
else()
    install(TARGETS lin_analyzer LIBRARY DESTINATION ".")
endif()
