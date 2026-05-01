cmake_minimum_required(VERSION 3.10)
project(unqlite LANGUAGES C)
include(GNUInstallDirs)

# If the user did not customize the install prefix,
# set it to live under build so we don't inadvertently pollute /usr/local
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "default install path" FORCE)
endif()

option(ENABLE_THREADS "Enable threads" OFF)

if(ENABLE_THREADS)
  add_definitions(-DUNQLITE_ENABLE_THREADS)
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR}/src)

set(HEADERS_UNQLITE
  unqlite.h
)
source_group("Header Files" FILES "${HEADERS_UNQLITE}")

set(SOURCES_UNQLITE
  unqlite.c
)
source_group("Source Files" FILES "${SOURCES_UNQLITE}")

set(UNQLITE_STATIC_LIB unqlite)
add_library(${UNQLITE_STATIC_LIB} ${HEADERS_UNQLITE} ${SOURCES_UNQLITE})

install(TARGETS ${UNQLITE_STATIC_LIB} COMPONENT devel ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES ${HEADERS_UNQLITE} COMPONENT devel DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CTest)
if (BUILD_TESTING)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/samples/CMakeLists.txt")
    add_subdirectory(samples)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/example/CMakeLists.txt")
    add_subdirectory(example)
  endif()
endif()
