cmake_minimum_required(VERSION 3.5)

project(rme)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Boost 1.66.0 REQUIRED COMPONENTS date_time system filesystem iostreams)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_package(OpenGL REQUIRED)

# LibArchive disabled in compilation level by default, see "#define OTGZ_SUPPORT" in the "definitions.h" file
# If you need use, enable this:
#find_package(LibArchive REQUIRED)
#${LibArchive_INCLUDE_DIRS} ${LibArchive_LIBRARIES}

#if(APPLE)
#    set(CMAKE_PREFIX_PATH /usr/local/opt/libarchive)
#endif()

if(WIN32)
    set(Boost_THREADAPI win32)
endif()

find_package(wxWidgets COMPONENTS html aui gl adv core net base REQUIRED)

find_package(GLUT REQUIRED)
find_package(ZLIB REQUIRED)

include(${wxWidgets_USE_FILE})
include(source/CMakeLists.txt)

# Add Windows resource file for icon
if(WIN32)
    set(RME_RC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcproj/Project/RME.rc)
    add_executable(${PROJECT_NAME} WIN32 ${rme_H} ${rme_SRC} ${RME_RC_FILE})
else()
    add_executable(${PROJECT_NAME} ${rme_H} ${rme_SRC})
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)

include_directories(
    ${LibArchive_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    ${GLUT_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIR}

    )
target_link_libraries(${PROJECT_NAME}
    ${wxWidgets_LIBRARIES}
    ${LibArchive_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLUT_LIBRARIES}
    ${ZLIB_LIBRARIES}
    fmt::fmt
    Boost::date_time
    Boost::system
    Boost::filesystem
    Boost::iostreams
    nlohmann_json::nlohmann_json
)
