cmake_minimum_required(VERSION 3.4.3)
project(ulticontroller_interface_tester C)

find_package(Git)

include(GNUInstallDirs)

set(CMAKE_C_FLAGS "-Wall -Werror")

set(GIT_VERSION "Unknown")
if(GIT_FOUND)
    message("git found: ${GIT_EXECUTABLE}")
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --dirty --tags --always OUTPUT_VARIABLE GIT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

configure_file(${CMAKE_SOURCE_DIR}/src/version.h.in ${CMAKE_SOURCE_DIR}/src/version.h)

add_executable(ucit src/ucit.c)

install(PROGRAMS "${CMAKE_BINARY_DIR}/ucit"
	DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")

install(FILES "${CMAKE_SOURCE_DIR}/scripts/systemd/ucit@.service"
	DESTINATION "/lib/systemd/system"
)

if(EXISTS "${CMAKE_ROOT}/Modules/CPack.cmake")
	set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "UltiController Interface Tester")
	set(CPACK_PACKAGE_VENDOR "Ultimaker")
	set(CPACK_PACKAGE_CONTACT "Ultimaker <info@ultimaker.com>")
	if(NOT DEFINED CPACK_PACKAGE_VERSION)
		set(CPACK_PACKAGE_VERSION "9999.99.99.${GIT_VERSION}")
	endif()
	include(CPackConfig.cmake)
	include(CPack)
endif()
