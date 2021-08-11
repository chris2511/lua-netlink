/*
 * Copyright (c) 2021 Christian Hohnstaedt
 * SPDX-License-Identifier: MIT
 */

#include <lua.h>
#include <lualib.h>

#include <libmnl/libmnl.h>
#include <linux/rtnetlink.h>

#include "netlink.h"

static int parse_attr(const struct nlattr *attr, void *data)
{
	struct callback_data *cbd = data;
	int type = mnl_attr_get_type(attr);

	switch (type) {
	case IFA_LOCAL:
	case IFA_ADDRESS:
		push_ip(cbd->L, "ip", cbd->ifa->ifa_family, attr);
	}
	return MNL_CB_OK;
}

static int ifaddr_cb(const struct nlmsghdr *nlh, struct callback_data *cbd)
{
	lua_pushliteral(cbd->L, "index");
	lua_pushinteger(cbd->L, cbd->ifa->ifa_index);
	lua_settable(cbd->L, -3);

	lua_pushliteral(cbd->L, "family");
	if (cbd->ifa->ifa_family == AF_INET)
		lua_pushliteral(cbd->L, "AF_INET");
	else
		lua_pushliteral(cbd->L, "AF_INET6");
	lua_settable(cbd->L, -3);

	return mnl_attr_parse(nlh, sizeof(*cbd->ifa), parse_attr, cbd);
}

struct rtmgrp ifaddr_rtmgrp = {
	"ifaddr", RTMGRP_IPV4_IFADDR, ifaddr_cb,
	RTM_NEWADDR, RTM_DELADDR, RTM_GETADDR
};
LUA_RTMGRP(ifaddr_rtmgrp);
