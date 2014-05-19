CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(vmodem-plugin C)

### Global setting ###
SET(PREFIX ${CMAKE_INSTALL_PREFIX})
SET(EXEC_PREFIX "\${prefix}")
SET(LIBDIR "\${prefix}/lib")
SET(INCLUDEDIR "\${prefix}/include")
SET(PKGCONFIGDIR "${PREFIX}/${LIB_INSTALL_DIR}/pkgconfig" CACHE PATH PKGCONFIGDIR)
SET(CMAKE_INSTALL_PREFIX "${PREFIX}")

# Set required packages
INCLUDE(FindPkgConfig)
pkg_check_modules(pkgs REQUIRED glib-2.0 tcore tel-headers)

FOREACH(flag ${pkgs_CFLAGS})
	SET(EXTRA_CFLAGS "${EXTRA_CFLAGS} ${flag}")
ENDFOREACH(flag)

INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/include/)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EXTRA_CFLAGS} -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wdeclaration-after-statement -Wmissing-declarations -Wredundant-decls -Wcast-align")


ADD_DEFINITIONS("-DFEATURE_TLOG_DEBUG")
ADD_DEFINITIONS("-DTCORE_LOG_TAG=\"VMODEM\"")
ADD_DEFINITIONS("-DEXPORT_API=__attribute__((visibility(\"default\")))")

MESSAGE(${CMAKE_C_FLAGS})
MESSAGE(${CMAKE_EXE_LINKER_FLAGS})

SET(SRCS
	src/desc-vmodem.c
	src/vdpram.c
	src/config.c
	src/vdpram_dump.c
)


# library build
ADD_LIBRARY(vmodem-plugin SHARED ${SRCS})
TARGET_LINK_LIBRARIES(vmodem-plugin ${pkgs_LDFLAGS})
SET_TARGET_PROPERTIES(vmodem-plugin PROPERTIES PREFIX "" OUTPUT_NAME vmodem-plugin)


# install
INSTALL(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION ${LIB_INSTALL_DIR}/telephony/plugins)
INSTALL(FILES ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION /usr/share/license RENAME tel-plugin-vmodem)
