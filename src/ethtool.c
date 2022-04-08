/*
 * Copyright (c) 2021 Christian Hohnstaedt
 * SPDX-License-Identifier: MIT
 */

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/ioctl.h>

#include <netinet/in.h>

#include <linux/if.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>

#include "netlink.h"

int netlink_ethtool(lua_State *L)
{
	struct ethtool_cmd req;
	struct ifreq ifr;
	const char *ifname;
	size_t len;
	int fd, speed;

	if (lua_type(L, -1) == LUA_TSTRING) {
		/* Replace string with interface name by table with
		 * element "name" containing the interface name */
		lua_newtable(L);
		lua_pushliteral(L, "name");
		lua_pushvalue(L, -3);
		lua_settable(L, -3);
		lua_remove(L, -2);
	}
	lua_pushliteral(L, "name");
	lua_gettable(L, -2);
	ifname = lua_tolstring(L, -1, &len);
	lua_pop(L, 1);

	if (!ifname || len >= sizeof ifr.ifr_name)
		return 1;

	/* Setup our control structures. */
	memcpy(ifr.ifr_name, ifname, len+1);

	/* Open control socket. */
	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0)
		return luaL_error(L, "socket(IPPROTO_IP): %s", strerror(errno));

	memset(&req, 0, sizeof(req));
	req.cmd = ETHTOOL_GSET;

	ifr.ifr_data = &req;
	if (ioctl(fd, SIOCETHTOOL, &ifr) < 0) {
		close(fd);
		if (errno != ENOTSUP)
			return luaL_error(L, "ioctl(SIOCETHTOOL, '%s'): %s",
					ifname, strerror(errno));

		return 1;
	}
	close(fd);

	speed = ethtool_cmd_speed(&req);
	push_integer(L, "speed", speed == -1 ? 0 : speed);
	push_string(L, "duplex", req.duplex ? "full" : "half");
	push_bool(L, "autoneg", req.autoneg);

	return 1;
}
