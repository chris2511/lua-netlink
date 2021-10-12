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
	push_integer(cbd->L, "index", cbd->ifa->ifa_index);
	push_string(cbd->L, "family", cbd->ifa->ifa_family == AF_INET ?
					"AF_INET" : "AF_INET6");

	return mnl_attr_parse(nlh, sizeof(*cbd->ifa), parse_attr, cbd);
}

struct rtmgrp ifaddr_rtmgrp = {
	"ifaddr", RTMGRP_IPV4_IFADDR, ifaddr_cb,
	RTM_NEWADDR, RTM_DELADDR, RTM_GETADDR
};
LUA_RTMGRP(ifaddr_rtmgrp);
