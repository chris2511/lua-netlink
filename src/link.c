/*
 * Copyright (c) 2021 Christian Hohnstaedt
 * SPDX-License-Identifier: MIT
 */

#include <lua.h>
#include <lualib.h>

#include <libmnl/libmnl.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>

#include "netlink.h"

static int parse_attr(const struct nlattr *attr, void *data)
{
	struct callback_data *cbd = data;
	lua_State *L = cbd->L;
	int type = mnl_attr_get_type(attr);

	switch (type) {
	case IFLA_MTU:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0)
			return MNL_CB_ERROR;
		lua_pushliteral(L, "mtu");
		lua_pushinteger(L, mnl_attr_get_u32(attr));
		lua_settable(L, -3);
		break;
	case IFLA_IFNAME:
		if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0)
			return MNL_CB_ERROR;
		lua_pushliteral(L, "name");
		lua_pushstring(L, mnl_attr_get_str(attr));
		lua_settable(L, -3);
		break;
	case IFLA_ADDRESS: {
		if (mnl_attr_validate(attr, MNL_TYPE_BINARY) < 0)
			return MNL_CB_ERROR;

		push_hwaddr(cbd->L, "hwaddr", attr);
		break;
		}
	}
	return MNL_CB_OK;
}

static int link_cb(const struct nlmsghdr *nlh, struct callback_data *cbd)
{
	lua_pushliteral(cbd->L, "index");
	lua_pushinteger(cbd->L, cbd->ifm->ifi_index);
	lua_settable(cbd->L, -3);

	lua_pushliteral(cbd->L, "up");
	lua_pushboolean(cbd->L, cbd->ifm->ifi_flags & IFF_UP);
	lua_settable(cbd->L, -3);

	lua_pushliteral(cbd->L, "running");
	lua_pushboolean(cbd->L, cbd->ifm->ifi_flags & IFF_RUNNING);
	lua_settable(cbd->L, -3);

	if (nlh->nlmsg_type == RTM_NEWLINK && cbd->ifm->ifi_flags & IFF_RUNNING)
		netlink_ethtool(cbd->L);

	return mnl_attr_parse(nlh, sizeof(*cbd->ifm), parse_attr, cbd);
}

struct rtmgrp link_rtmgrp = {
	"link", RTMGRP_LINK, link_cb,
	RTM_NEWLINK, RTM_DELLINK, RTM_GETLINK
};
LUA_RTMGRP(link_rtmgrp);
