project(kronos)

cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)

#this is because multiple ports are atm not buildable in parallel
set(YAB_PORTS "" CACHE STRING "List of ports to build" FORCE)

if(WIN32 OR LINUX)
	set(HAVE_CDROM ON)
	add_definitions(-DHAVE_CDROM -DHAVE_FSEEKO)
endif()

set(KRONOS_LIBRETRO_SRCS 
	${KRONOS_LIBRETRO_SRCS}

	glsym_private.c
	libretro.c
	${LIBRETRO_COMM_DIR}/compat/compat_posix_string.c
	${LIBRETRO_COMM_DIR}/compat/compat_strl.c
	${LIBRETRO_COMM_DIR}/compat/fopen_utf8.c
	${LIBRETRO_COMM_DIR}/encodings/encoding_utf.c
	${LIBRETRO_COMM_DIR}/file/file_path.c
	${LIBRETRO_COMM_DIR}/file/file_path_io.c
	${LIBRETRO_COMM_DIR}/compat/compat_strcasestr.c
	${LIBRETRO_COMM_DIR}/vfs/vfs_implementation.c
	${LIBRETRO_COMM_DIR}/streams/file_stream.c
	${LIBRETRO_COMM_DIR}/time/rtime.c
	${LIBRETRO_COMM_DIR}/rthreads/rthreads.c
	${LIBRETRO_COMM_DIR}/glsm/glsm.c 
	${LIBRETRO_COMM_DIR}/glsym/rglgen.c 
	${LIBRETRO_COMM_DIR}/string/stdstring.c
)

if(HAVE_CDROM)
	set(KRONOS_LIBRETRO_SRCS 
		${KRONOS_LIBRETRO_SRCS}

		${LIBRETRO_COMM_DIR}/vfs/vfs_implementation_cdrom.c 
		${LIBRETRO_COMM_DIR}/cdrom/cdrom.c 
		${LIBRETRO_COMM_DIR}/memmap/memalign.c 
		${LIBRETRO_COMM_DIR}/lists/string_list.c
		${LIBRETRO_COMM_DIR}/lists/dir_list.c 
		${LIBRETRO_COMM_DIR}/string/stdstring.c
		${LIBRETRO_COMM_DIR}/file/retro_dirent.c
		${LIBRETRO_COMM_DIR}/formats/libchdr/libchdr_chd.c
		${LIBRETRO_COMM_DIR}/formats/libchdr/libchdr_huffman.c
		${LIBRETRO_COMM_DIR}/formats/libchdr/libchdr_bitstream.c
		../core/cd/libretro/cd-libretro.c
	)
endif()

if(WIN32)
	include_directories(${CMAKE_SOURCE_DIR}/src/tools/libchdr/deps/flac-1.3.2/include)
	set(KRONOS_LIBRETRO_SRCS ${KRONOS_LIBRETRO_SRCS} ${CMAKE_SOURCE_DIR}/src/tools/libchdr/deps/flac-1.3.2/src/libFLAC/windows_unicode_filenames.c)
endif()

if (YAB_FORCE_GLES31 OR OPENGLES31_FOUND OR OPENGLES3_FOUND)
	set(KRONOS_LIBRETRO_SRCS ${KRONOS_LIBRETRO_SRCS} ${LIBRETRO_COMM_DIR}/glsym/glsym_es3.c)
	if (NOT OPENGLES31_FOUND)
		if(WIN32)
			message(WARNING "Failed to find OpenGLES. Targeting OpenGLES on Windows is not nessesarly usefull since most Windows systems wont support OpenGLES and rather support OpenGL. But try to workaround that anyways.")
			SET (OPENGLES_gl_LIBRARY import32 CACHE STRING "OpenGL ES 1.x library for win32")
		endif()
	endif()
else()
	set(KRONOS_LIBRETRO_SRCS ${KRONOS_LIBRETRO_SRCS} ${LIBRETRO_COMM_DIR}/glsym/glsym_gl.c)
endif()

set(KRONOS_LIBRETRO_PUBLIC_HDRS
	libretro_core_options.h
)

set(KRONOS_LIBRETRO_PRIVATE_HDRS
	${LIBRETRO_COMM_DIR}/include/compat/msvc.h
	glsym_private.h
)

add_library(kronos_libretro SHARED ${KRONOS_LIBRETRO_SRCS} ${KRONOS_LIBRETRO_PUBLIC_HDRS} ${KRONOS_LIBRETRO_PRIVATE_HDRS})
#the order is important for now which is just about the order of linking. some of the components still need to be exchanged when building a libretrocore
target_link_libraries(kronos_libretro kronos zlibstatic mini18n-static lzma-static)

add_dependencies(kronos_libretro kronos)

if(YAB_FORCE_GLES31 OR OPENGLES31_FOUND OR OPENGLES3_FOUND)
	target_link_libraries(kronos_libretro GLESv2)
endif()

if(WIN32)
	target_link_libraries(kronos_libretro opengl32.lib ws2_32.lib)
endif()

if(UNIX)
	target_compile_options(mini18n-static PUBLIC -fPIC)
	target_compile_options(zlibstatic PUBLIC -fPIC)
	target_compile_options(kronos PUBLIC -fPIC)
	target_compile_options(kronos_libretro PUBLIC -fPIC)
endif()