
cmake_minimum_required(VERSION 3.13.0)

project(netlink)

include(FindLua)
include(GNUInstallDirs)

# minimalistic netlink library
#find_library(Mnl_libs libmnl.a)
find_library(Mnl_libs mnl)
find_path(Mnl_header libmnl/libmnl.h)

#set(CMAKE_BUILD_TYPE Release)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(LUA_VERSION ${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR})

include_directories(${LUA_INCLUDE_DIR})

add_library(${CMAKE_PROJECT_NAME} SHARED netlink.c)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${Mnl_libs})
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${Mnl_header})
install(TARGETS netlink
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/lua/${LUA_VERSION})

