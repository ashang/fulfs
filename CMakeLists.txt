cmake_minimum_required(VERSION 2.6)
set(CMAKE_LEGACY_CYGWIN_WIN32 0)
PROJECT(fulfs CXX)


INCLUDE (CheckIncludeFiles)
INCLUDE(CheckCXXSourceCompiles)

# I don't need -rdynamic, thanks!
SET(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")
SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

# common required flags
SET(FLAGS_MIN " -pthread -Wall -D_FILE_OFFSET_BITS=64 ")
SET(CMAKE_REQUIRED_FLAGS " ${FLAGS_MIN} -Wl,--as-needed")
SET(TESTSRC "int main() {return 0;}")
CHECK_CXX_SOURCE_COMPILES("${TESTSRC}" HAVE_WL_AS_NEEDED)

ADD_EXECUTABLE(fulfs fulfs.cc lockable.cc)

IF(HAVE_WL_AS_NEEDED)
SET_TARGET_PROPERTIES(fulfs PROPERTIES LINK_FLAGS  -Wl,--as-needed)
ENDIF(HAVE_WL_AS_NEEDED)

#LINK_LIBRARIES(fuse)

include(FindPkgConfig)
pkg_check_modules(fuse fuse)
# what a mess... convert sepator back to space
foreach(arg ${fuse_CFLAGS})
   set(FLAGS_MIN "${FLAGS_MIN} ${arg}")
endforeach(arg ${fuse_CFLAGS})


SET(TESTSRC "
#define _FILE_OFFSET_BITS 64
#define FUSE_USE_VERSION 25
#include <fuse.h>
int main() { return 0; }
")
SET(CMAKE_REQUIRED_FLAGS ${fuse_CFLAGS})
CHECK_CXX_SOURCE_COMPILES("${TESTSRC}" HAVE_FUSE_25)

if(fuse_FOUND AND HAVE_FUSE_25)
   SET_TARGET_PROPERTIES(fulfs PROPERTIES COMPILE_FLAGS "${FLAGS_MIN}" )
   TARGET_LINK_LIBRARIES(fulfs ${fuse_LDFLAGS})
else(fuse_FOUND AND HAVE_FUSE_25)
   message(FATAL_ERROR "FUSE not found or incompatible, cannot build fulfs")
endif(fuse_FOUND AND HAVE_FUSE_25)
