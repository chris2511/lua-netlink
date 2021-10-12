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
		push_integer(L, "mtu", mnl_attr_get_u32(attr));
		break;
	case IFLA_IFNAME:
		if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0)
			return MNL_CB_ERROR;
		push_string(L, "name", mnl_attr_get_str(attr));
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
	push_integer(cbd->L, "index", cbd->ifm->ifi_index);
	push_bool(cbd->L, "running", cbd->ifm->ifi_flags & IFF_RUNNING);
	push_bool(cbd->L, "up", cbd->ifm->ifi_flags & IFF_UP);

	if (nlh->nlmsg_type == RTM_NEWLINK &&
	    cbd->ifm->ifi_flags & IFF_RUNNING)
		netlink_ethtool(cbd->L);

	return mnl_attr_parse(nlh, sizeof(*cbd->ifm), parse_attr, cbd);
}

struct rtmgrp link_rtmgrp = {
	"link", RTMGRP_LINK, link_cb,
	RTM_NEWLINK, RTM_DELLINK, RTM_GETLINK
};
LUA_RTMGRP(link_rtmgrp);
