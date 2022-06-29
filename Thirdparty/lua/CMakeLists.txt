project(Lua)

set(LUA_CORE_FILES
    src/lapi.c src/lcode.c src/lctype.c src/ldebug.c src/ldo.c src/ldump.c src/lfunc.c src/lgc.c src/llex.c 
	src/lmem.c src/lobject.c src/lopcodes.c src/lparser.c src/lstate.c src/lstring.c src/ltable.c 
	src/ltm.c src/lundump.c src/lvm.c src/lzio.c src/ltests.c
)
set(LUA_AUX_FILES 
    src/lauxlib.c
)
set(LUA_LIB_FILES
    src/lbaselib.c src/ldblib.c src/liolib.c src/lmathlib.c src/loslib.c src/ltablib.c src/lstrlib.c
    src/lutf8lib.c src/loadlib.c src/lcorolib.c src/linit.c
)

add_library(lua STATIC ${LUA_CORE_FILES} ${LUA_AUX_FILES} ${LUA_LIB_FILES})
set_target_properties(lua
PROPERTIES
    C_STANDARD 99
)
target_include_directories(lua
PUBLIC
    include
)

if (WIN32)
    target_compile_definitions(lua PRIVATE -DLUA_USE_WINDOWS)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_compile_definitions(lua PRIVATE -DLUA_USE_LINUX)
    target_link_libraries(lua PUBLIC dl)
elseif (APPLE)
    target_compile_definitions(lua PRIVATE -DLUA_USE_MACOSX)
else ()
    message(FATAL_ERROR "Unknown platform for lua!")
endif ()
