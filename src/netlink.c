/*
 * Copyright (c) 2021 Christian Hohnstaedt
 * SPDX-License-Identifier: MIT
 */

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>

#include <sys/socket.h>
#include <sys/ioctl.h>

#include <libmnl/libmnl.h>
#include <linux/netlink.h>

#include "netlink.h"

struct userdata {
	struct mnl_socket *nl;
	int groups;
};

/* Callback function for each netlink message
 * Iterates over all "struct rtmgrp" and checks whether
 * the "nlmsg_type" is their "new" or "del" type and calls
 * the registered callback of the group.
 * The "stamp" and "event" values are set here for all
 */
static int data_cb(const struct nlmsghdr *nlh, void *Ldata)
{
	lua_State *L = Ldata;
	struct callback_data cbd = {
		.L = L,
		.nl_payload = mnl_nlmsg_get_payload(nlh),
	};
	int ret = MNL_CB_STOP, top;
	struct timespec tp;
	struct rtmgrp *rtmgrp;

	top = lua_gettop(L);
	lua_newtable(L);

	clock_gettime(CLOCK_MONOTONIC, &tp);

	push_integer(L, "stamp", tp.tv_nsec /(1000*1000) + tp.tv_sec *1000);

	for (rtmgrp = &__start_rtmgrp; rtmgrp < &__stop_rtmgrp; rtmgrp++) {
		const char *eventtype;

		if (nlh->nlmsg_type == rtmgrp->new)
			eventtype = "new";
		else if (nlh->nlmsg_type == rtmgrp->del)
			eventtype = "del";
		else
			continue;

		lua_pushliteral(L, "event");
		lua_pushfstring(L, "%s%s", eventtype, rtmgrp->name);
		lua_rawset(L, -3);

		ret = rtmgrp->callback(nlh, &cbd);
		break;
	}
	if (ret != MNL_CB_OK)
		lua_settop(L, top);
	else
		lua_seti(L, 2, luaL_len(L, 2) +1);

	return MNL_CB_OK;
}

/* Receive a netlink message in non-blocking mode.
 * It stops on "EBUSY" and "EAGAIN" or if the callback returns MNL_CB_STOP.
 * In case of any other I/O error a lua error is thrown
 */
int receive(struct mnl_socket *nl, lua_State *L)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	int ret;

	do {
		ret = mnl_socket_recvfrom(nl, buf, sizeof buf);
		if (ret == -1)
			break;
		ret = mnl_cb_run(buf, ret, 0, 0, data_cb, L);
		if (ret == -1) {
			if  (errno == EBUSY || errno == EAGAIN)
				ret = MNL_CB_STOP;
			else
				return luaL_error(L, "mnl_cb_run(): %s",
							strerror(errno));
		}
	} while (ret == MNL_CB_OK);

	return MNL_CB_OK;
}

/* iterates over a lua set of rtmgrp names ("ifaddr", "link", ...)
 * and puts the group bit RTMGRP_IPV4_IFADDR, RTMGRP_LINK, ...
 * into the groups bitfield
 */
static int groups_from_set(lua_State *L, int idx)
{
	int groups = 0;
	struct rtmgrp *rtmgrp;

	for (rtmgrp = &__start_rtmgrp; rtmgrp < &__stop_rtmgrp; rtmgrp++) {
		lua_pushstring(L, rtmgrp->name);
		lua_gettable(L, idx);
		if (lua_toboolean(L, -1))
			groups |= rtmgrp->group;
		lua_pop(L, 1);
	}
	return groups;
}

/* The "netlink table" is expected as first argument (by calling nl:...)
 */
static struct userdata *get_userdata(lua_State *L)
{
	return luaL_checkudata(L, 1, "mnl.netlink");
}

/* Returns an array of registered rtmgrp members like
 * "ifaddr", "link", "route", "arp" for this netlink socket.
 */
static int nlfunc_groups(lua_State *L)
{
	int j = 1;
	struct rtmgrp *rtmgrp;
	struct userdata *userdata = get_userdata(L);

	lua_newtable(L);

	for (rtmgrp = &__start_rtmgrp; rtmgrp < &__stop_rtmgrp; rtmgrp++) {
		if (userdata->groups & rtmgrp->group) {
			lua_pushstring(L, rtmgrp->name);
			lua_seti(L, -2, j++);
		}
	}
	return 1;
}

/* Request current values of all or a list of groups
 * This may be called during start to initially retrieve all current values.
 */
static int nlfunc_query(lua_State *L)
{
	struct userdata *userdata = get_userdata(L);
	int groups = userdata->groups;
	struct rtmgrp *rtmgrp;

	if (lua_istable(L, 2))
		groups = groups_from_set(L, 2);

	lua_settop(L, 1);
	lua_newtable(L);

	for (rtmgrp = &__start_rtmgrp; rtmgrp < &__stop_rtmgrp; rtmgrp++) {
		if (groups & rtmgrp->group) {
			int ret = netlink_initial(userdata->nl, L, rtmgrp->get);
			if (ret != MNL_CB_OK)
				break;
		}
	}
	return 1;
}

/* Retrieves events about changed values and triggers the callbacks */
static int nlfunc_event(lua_State *L)
{
	struct userdata *userdata = get_userdata(L);

	lua_settop(L, 1);
	lua_newtable(L);

	receive(userdata->nl, L);
	return 1;
}

/* Returns the netlink filedescriptor to be used in poll or select
 * e.g luaposix poll().
 * Don't close it manually
 */
static int nlfunc_fd(lua_State *L)
{
	struct userdata *userdata = get_userdata(L);
	lua_pushinteger(L, mnl_socket_get_fd(userdata->nl));
	return 1;
}

/* Wait for the next netlink event */
static int nlfunc_poll(lua_State *L)
{
	struct userdata *userdata = get_userdata(L);
	int tout = -1, ret;
	struct pollfd pfd = {
		.fd = mnl_socket_get_fd(userdata->nl),
		.events = POLLIN | POLLPRI,
	};

	if (lua_isinteger(L, 2))
		tout = lua_tointeger(L, 2);

	ret = poll(&pfd, 1, tout);
	if (ret == -1)
		return luaL_error(L, "poll(): %s\n", strerror(errno));
	lua_pushboolean(L, ret);
	return 1;
}

/* Create a new "netlink socket" userdata with the "mnl_socket_functions[]"
 * as methods via metatable
 */
static int netlink_socket(lua_State *L)
{
	struct mnl_socket *nl;
	struct userdata *userdata;
	int groups = 0;

	if (lua_istable(L, 1)) {
		groups = groups_from_set(L, 1);
	} else {
		for (struct rtmgrp *rtmgrp = &__start_rtmgrp;
		     rtmgrp < &__stop_rtmgrp; rtmgrp++)
		{
			groups |= rtmgrp->group;
		}
	}
	if (!groups)
		return luaL_error(L, "No netlink groups");

	nl = mnl_socket_open2(NETLINK_ROUTE, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (nl == NULL)
		return luaL_error(L, "mnl_socket_open(): %s", strerror(errno));

	if (mnl_socket_bind(nl, groups, MNL_SOCKET_AUTOPID) < 0) {
		int errn = errno;
		mnl_socket_close(nl);
		return luaL_error(L, "mnl_socket_bind(%d): %s",
					groups,strerror(errn));
	}

	userdata = lua_newuserdata(L, sizeof *userdata);
	userdata->nl = nl;
	userdata->groups = groups;
	/* The garbage collector closes the mnl file descriptor */
	luaL_setmetatable(L, "mnl.netlink");

	return 1;
}

/* Returns an array of all known rtmgrp members like
 * "ifaddr", "link", "route", "arp"
 */
static int netlink_groups(lua_State *L)
{
	struct rtmgrp *rtmgrp;
	int i = 1;

	lua_newtable(L);

	for (rtmgrp = &__start_rtmgrp; rtmgrp < &__stop_rtmgrp; rtmgrp++) {
		lua_pushstring(L, rtmgrp->name);
		lua_seti(L, -2, i++);
	}
	return 1;
}

/* Garbage collector for the "netlink socket" userdata */
static int userdata_gc(lua_State *L)
{
	struct userdata *userdata = lua_touserdata(L, 1);
	mnl_socket_close(userdata->nl);
	return 0;
}

static const struct luaL_Reg netlink_functions[] = {
	{ "socket", netlink_socket },
	{ "ethtool", netlink_ethtool },
	{ "groups", netlink_groups },
	{ NULL, NULL }
};

static const struct luaL_Reg mnl_socket_functions[] = {
	{ "fd", nlfunc_fd },
	{ "event", nlfunc_event },
	{ "query", nlfunc_query },
	{ "groups", nlfunc_groups },
	{ "poll", nlfunc_poll },
	{ NULL, NULL }
};

EXPORT_SYMBOL(luaopen_netlink);
int luaopen_netlink(lua_State *L)
{
	if (luaL_newmetatable(L, "mnl.netlink")) {
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, userdata_gc);
		lua_rawset(L, -3);
		lua_pushliteral(L, "__index");
		luaL_newlib(L, mnl_socket_functions);
		lua_pushliteral(L, "version");
		lua_pushliteral(L, VERSION);
		lua_rawset(L, -3);

		lua_rawset(L, -3);
	}
	luaL_newlib(L, netlink_functions);
	return 1;
}
