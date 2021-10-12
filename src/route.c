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

	switch(type) {
	case RTA_SRC:
		push_cidr(cbd->L, "src", cbd->rtm->rtm_family,
				attr, cbd->rtm->rtm_src_len);
		break;
	case RTA_DST:
		push_cidr(cbd->L, "dst", cbd->rtm->rtm_family,
				attr, cbd->rtm->rtm_dst_len);
		break;
	case RTA_GATEWAY:
		push_ip(cbd->L, "gateway", cbd->rtm->rtm_family, attr);
		break;
	}
	return MNL_CB_OK;
}

static int route_cb(const struct nlmsghdr *nlh, struct callback_data *cbd)
{
	if (cbd->rtm->rtm_type != RTN_UNICAST)
		return MNL_CB_STOP;

	push_integer(cbd->L, "scope", cbd->rtm->rtm_scope);
	return mnl_attr_parse(nlh, sizeof(*cbd->rtm), parse_attr, cbd);
}

struct rtmgrp route_rtmgrp = {
	"route", RTMGRP_IPV4_ROUTE, route_cb,
	RTM_NEWROUTE, RTM_DELROUTE, RTM_GETROUTE
};
LUA_RTMGRP(route_rtmgrp);
