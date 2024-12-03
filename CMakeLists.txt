
cmake_minimum_required(VERSION 3.13.0)

project(netlink VERSION 1.1.0
	DESCRIPTION "Access network information via netlink"
)

include(FindLua)
# minimalistic netlink library
#find_library(Mnl_libs libmnl.a)
find_library(Mnl_libs mnl)
find_path(Mnl_header libmnl/libmnl.h)

add_compile_options(-Wall -Wextra -pedantic -Werror -fvisibility=hidden -Waggregate-return -Wmissing-prototypes -Wshadow -Wstrict-prototypes -DVERSION="${PROJECT_VERSION}")
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")

include_directories(${LUA_INCLUDE_DIR} src)

add_library(${CMAKE_PROJECT_NAME} SHARED
	src/netlink.c src/lib.c src/ethtool.c src/link.c src/ifaddr.c
	src/route.c src/neigh.c
)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${Mnl_libs})
target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC ${Mnl_header})

if (NOT DEFINED LUA_LIBDIR)
  set(LUA_LIBDIR "/usr/local/lib/lua/${LUA_VERSION_MAJOR}.${LUA_VERSION_MINOR}")
endif()

install(TARGETS netlink DESTINATION ${LUA_LIBDIR})
