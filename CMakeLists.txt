CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)
PROJECT(unqlite)
INCLUDE(GNUInstallDirs)

# If the user did not customize the install prefix,
# set it to live under build so we don't inadvertently pollute /usr/local
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  SET(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "default install path" FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

OPTION(ENABLE_THREADS "Enable threads" OFF)

IF(${ENABLE_THREADS})
  ADD_DEFINITIONS("-DUNQLITE_ENABLE_THREADS")
ENDIF(${ENABLE_THREADS})

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR}/src)

SET(HEADERS_UNQLITE
  unqlite.h
)
source_group("Header Files" FILES "${HEADERS_UNQLITE}")

SET(SOURCES_UNQLITE
  unqlite.c
)
source_group("Source Files" FILES "${SOURCES_UNQLITE}")

SET(UNQLITE_STATIC_LIB unqlite)
ADD_LIBRARY(${UNQLITE_STATIC_LIB} ${HEADERS_UNQLITE} ${SOURCES_UNQLITE})

INSTALL(TARGETS ${UNQLITE_STATIC_LIB} COMPONENT devel ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
INSTALL(FILES ${HEADERS_UNQLITE} COMPONENT devel DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CTest)
if (BUILD_TESTING)
  add_subdirectory("example")
endif (BUILD_TESTING)
