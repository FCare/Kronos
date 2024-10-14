cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)
project(kronos)


#-------------------------------------------------------------------------------
#add local find scripts to CMAKE path
LIST(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/CMake/Utils
    ${CMAKE_CURRENT_SOURCE_DIR}/CMake/Packages
)

set(YAB_PACKAGE kronos)
set(YAB_VERSION_MAJOR 2)
set(YAB_VERSION_MINOR 6)
set(YAB_VERSION_PATCH 2)
set(YAB_VERSION "${YAB_VERSION_MAJOR}.${YAB_VERSION_MINOR}.${YAB_VERSION_PATCH}")

set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_PACKAGE_VERSION_MAJOR ${YAB_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${YAB_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${YAB_VERSION_PATCH})
set(CPACK_PACKAGE_VENDOR "Francois CARON")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "kronos-${YAB_VERSION}")

if (WIN32)
	SET(CPACK_NSIS_INSTALLED_ICON_NAME kronos.exe)
	set(CPACK_NSIS_MENU_LINKS kronos.exe;Kronos)
	set(CPACK_NSIS_URL_INFO_ABOUT "http://www.kronos-emulator.org/")
	set(CPACK_NSIS_COMPRESSOR "/SOLID lzma")
	set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "OFF")
	if(${CMAKE_SIZEOF_VOID_P} EQUAL 8)
		set(CPACK_SYSTEM_NAME "win64")
		set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
		set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "${CPACK_PACKAGE_NAME}${CPACK_PACKAGE_VERSION} (Win64)")
	endif ()
	 include(InstallRequiredSystemLibraries)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    add_custom_command(TARGET kronos POST_BUILD
                       COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/qtDeploy/"
                       COMMAND "${CMAKE_COMMAND}" -E
                               env PATH="${_qt_bin_dir}" "${WINDEPLOYQT_EXECUTABLE}"
                               --verbose 0
                               --no-compiler-runtime
                               --no-angle
                               --no-webkit2
                               --no-quick-import
                               --no-translations
                               --dir "${CMAKE_CURRENT_BINARY_DIR}/qtDeploy/" $<TARGET_FILE:kronos>
                       COMMENT "Deploying Qt..."
    )

    install(
        DIRECTORY "${CMAKE_BINARY_DIR}/qtDeploy/"
        DESTINATION .
    )
    set(CPACK_GENERATOR "ZIP;NSIS")
endif ()

include(CPack)

#add_subdirectory(doc)
add_subdirectory(src)
