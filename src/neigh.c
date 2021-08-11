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
	case NDA_DST:
		push_ip(cbd->L, "ip", cbd->ndm->ndm_family, attr);
		break;
	case NDA_LLADDR:
		push_hwaddr(cbd->L, "hwaddr", attr);
		break;
	case NDA_PROBES:
		if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0)
			return MNL_CB_ERROR;
		lua_pushliteral(cbd->L, "probes");
		lua_pushinteger(cbd->L, mnl_attr_get_u32(attr));
		lua_settable(cbd->L, -3);
	}
	return MNL_CB_OK;
}

static int neigh_cb(const struct nlmsghdr *nlh, struct callback_data *cbd)
{
	const char *nud_state;

	switch (cbd->ndm->ndm_state) {
	case NUD_REACHABLE: nud_state = "reachable"; break;
	case NUD_STALE:     nud_state = "stale"; break;
	case NUD_PROBE:     nud_state = "probe"; break;
	case NUD_FAILED:    nud_state = "failed"; break;
	case NUD_PERMANENT: nud_state = "permanent"; break;
	default:
		return MNL_CB_STOP;
	}

	lua_pushliteral(cbd->L, "index");
	lua_pushinteger(cbd->L, cbd->ndm->ndm_ifindex);
	lua_settable(cbd->L, -3);

	lua_pushliteral(cbd->L, "state");
	lua_pushstring(cbd->L, nud_state);
	lua_settable(cbd->L, -3);

	return mnl_attr_parse(nlh, sizeof(*cbd->ndm), parse_attr, cbd);
}

struct rtmgrp neigh_rtmgrp = {
	"neigh", RTMGRP_NEIGH, neigh_cb,
	RTM_NEWNEIGH, RTM_DELNEIGH, RTM_GETNEIGH
};
LUA_RTMGRP(neigh_rtmgrp);
