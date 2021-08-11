/*
 * Copyright (c) 2021 Christian Hohnstaedt
 * SPDX-License-Identifier: MIT
 */

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/ioctl.h>

#include <arpa/inet.h>

#include <libmnl/libmnl.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <linux/sockios.h>
#include <linux/ethtool.h>

struct callback_data {
	lua_State *L;
	union {
		struct rtmsg *rtm;
		struct ifinfomsg *ifm;
		struct ifaddrmsg *ifa;
		struct ndmsg *ndm;
		void *nl_payload;;
	};
};

static int do_ethtool(lua_State *L)
{
	struct ethtool_cmd req;
	struct ifreq ifr;
	const char *ifname;
	size_t len;
	int fd, ret;

	if (lua_type(L, -1) == LUA_TSTRING) {
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
		return luaL_error(L, "socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)");

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

	lua_pushliteral(L, "speed");
	lua_pushinteger(L, ethtool_cmd_speed(&req));
	lua_settable(L, -3);

	lua_pushliteral(L, "duplex");
	lua_pushstring(L, req.duplex ? "full" : "half");
	lua_settable(L, -3);

	lua_pushliteral(L, "autoneg");
	lua_pushboolean(L, req.autoneg);
	lua_settable(L, -3);

	return 1;
}

static void push_ip(lua_State *L, const char *which, int family,
			const struct nlattr *attr)
{
	char buf[INET6_ADDRSTRLEN];
	inet_ntop(family, mnl_attr_get_payload(attr), buf, sizeof buf);
	lua_pushstring(L, which);
	lua_pushstring(L, buf);
	lua_settable(L, -3);
}

static void push_cidr(lua_State *L, const char *which, int family,
			const struct nlattr *attr, int cidr)
{
	char buf[INET6_ADDRSTRLEN];
	inet_ntop(family, mnl_attr_get_payload(attr), buf, sizeof buf);
	lua_pushstring(L, which);
	lua_pushfstring(L, "%s/%d", buf, cidr);
	lua_settable(L, -3);
}

static void push_hwaddr(lua_State *L, const char *which,
			const struct nlattr *attr)
{
	uint8_t *hwaddr = mnl_attr_get_payload(attr);
	char addr[20], *p = addr;
	int i, max = mnl_attr_get_payload_len(attr);

	for (i = 0; i < max; i++) {
		p += sprintf(p, "%02x", hwaddr[i]);
		if (i + 1 != max)
			*p++ = ':';
	}
	lua_pushstring(L, which);
	lua_pushstring(L, addr);
	lua_settable(L, -3);
}

static int data_addr_cb(const struct nlattr *attr, void *data)
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

static int data_neigh_cb(const struct nlattr *attr, void *data)
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

static int data_link_cb(const struct nlattr *attr, void *data)
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

static int data_route_cb(const struct nlattr *attr, void *data)
{
	struct callback_data *cbd = data;
	int type = mnl_attr_get_type(attr);
	char buf[INET6_ADDRSTRLEN];
	struct in_addr ina;

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

static int data_cb(const struct nlmsghdr *nlh, void *Ldata)
{
	lua_State *L = Ldata;
	struct callback_data cbd = {
		.L = L,
		.nl_payload = mnl_nlmsg_get_payload(nlh),
	};
	int ret = MNL_CB_STOP, continu, del = 0;
	const char *event = "unknown", *nud_state;
	struct timespec tp;

	lua_settop(L, 1);
	lua_pushvalue(L, 1);
	lua_newtable(L);

	clock_gettime(CLOCK_MONOTONIC, &tp);

	lua_pushliteral(L, "clock");
	lua_pushinteger(L, tp.tv_nsec /(1000*1000) + tp.tv_sec *1000);
	lua_settable(L, -3);

	switch (nlh->nlmsg_type) {
	case RTM_DELROUTE: event = "delroute"; break;
	case RTM_NEWROUTE: event = "newroute"; break;
	case RTM_DELLINK:  event = "dellink"; break;
	case RTM_NEWLINK:  event = "newlink"; break;
	case RTM_DELADDR:  event = "deladdr"; break;
	case RTM_NEWADDR:  event = "newaddr"; break;
	case RTM_DELNEIGH: event = "delneigh"; break;
	case RTM_NEWNEIGH: event = "newneigh"; break;
	}

	switch (nlh->nlmsg_type) {
	case RTM_DELROUTE:
	case RTM_NEWROUTE:
		if (cbd.rtm->rtm_type != RTN_UNICAST)
			return MNL_CB_OK;

		lua_pushliteral(L, "scope");
		lua_pushinteger(L, cbd.rtm->rtm_scope);
		lua_settable(L, -3);

		ret = mnl_attr_parse(nlh, sizeof(*cbd.rtm),
					data_route_cb, &cbd);
		break;
	case RTM_DELLINK:
	case RTM_NEWLINK:
		lua_pushliteral(L, "index");
		lua_pushinteger(L, cbd.ifm->ifi_index);
		lua_settable(L, -3);

		lua_pushliteral(L, "up");
		lua_pushboolean(L, cbd.ifm->ifi_flags & IFF_UP);
		lua_settable(L, -3);

		lua_pushliteral(L, "running");
		lua_pushboolean(L, cbd.ifm->ifi_flags & IFF_RUNNING);
		lua_settable(L, -3);

		ret = mnl_attr_parse(nlh, sizeof(*cbd.ifm), data_link_cb, &cbd);

		if (nlh->nlmsg_type == RTM_NEWLINK &&
		    cbd.ifm->ifi_flags & IFF_RUNNING)
			do_ethtool(L);
		break;
	case RTM_DELADDR:
	case RTM_NEWADDR:
		lua_pushliteral(L, "index");
		lua_pushinteger(L, cbd.ifa->ifa_index);
		lua_settable(L, -3);

		lua_pushliteral(L, "family");
		if (cbd.ifa->ifa_family == AF_INET)
			lua_pushliteral(L, "AF_INET");
		else
			lua_pushliteral(L, "AF_INET6");
		lua_settable(L, -3);
		ret = mnl_attr_parse(nlh, sizeof(*cbd.ifa),
					data_addr_cb, &cbd);
		break;
	case RTM_DELNEIGH:
	case RTM_NEWNEIGH:
		switch (cbd.ndm->ndm_state) {
		case NUD_REACHABLE: nud_state = "reachable"; break;
		case NUD_STALE:     nud_state = "stale"; break;
		case NUD_PROBE:     nud_state = "probe"; break;
		case NUD_FAILED:    nud_state = "failed"; break;
		case NUD_PERMANENT: nud_state = "permanent"; break;
		default: return MNL_CB_OK;
		}

		lua_pushliteral(L, "index");
		lua_pushinteger(L, cbd.ndm->ndm_ifindex);
		lua_settable(L, -3);

		lua_pushliteral(L, "state");
		lua_pushstring(L, nud_state);
		lua_settable(L, -3);

		ret = mnl_attr_parse(nlh, sizeof(*cbd.ndm),
					data_neigh_cb, &cbd);
		break;
	default:
		return MNL_CB_OK;
	}
	if (ret != MNL_CB_OK)
		return ret;

	lua_pushliteral(L, "event");
	lua_pushstring(L, event);
	lua_settable(L, -3);

	lua_call(L, 1, 1);
	continu = lua_toboolean(L, -1);
	return continu ? MNL_CB_OK : MNL_CB_STOP;
}

static int receive(struct mnl_socket *nl, lua_State *L)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	int ret;

	do {
		ret = mnl_socket_recvfrom(nl, buf, sizeof buf);
		if (ret == -1)
			break;
		ret = mnl_cb_run(buf, ret, 0, 0, data_cb, L);
		if (ret == -1 && errno == EBUSY)
			ret = MNL_CB_OK;
	} while (ret == MNL_CB_OK);

	if (ret >= 0)
		return lua_toboolean(L, -1) ? MNL_CB_OK : MNL_CB_STOP;
	return ret;
}

static int netlink_initial_generic(struct mnl_socket *nl, lua_State *L,
				int type, void *data, size_t datalen)
{
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	void *dst;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= type;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = 0;
	dst = mnl_nlmsg_put_extra_header(nlh, datalen);
	memcpy(dst, data, datalen);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0)
		return -1;

	return receive(nl, L);
}

static int netlink_initial_addr(struct mnl_socket *nl, lua_State *L)
{
	struct ifaddrmsg ifm = { .ifa_family = AF_INET };
	return netlink_initial_generic(nl, L, RTM_GETADDR, &ifm, sizeof ifm);
}

static int netlink_initial_link(struct mnl_socket *nl, lua_State *L)
{
	struct rtgenmsg rt = { .rtgen_family = AF_PACKET};
	return netlink_initial_generic(nl, L, RTM_GETLINK, &rt, sizeof rt);
}

static int netlink_initial_route(struct mnl_socket *nl, lua_State *L)
{
	struct rtmsg rt = { .rtm_family = AF_INET};
	return netlink_initial_generic(nl, L, RTM_GETROUTE, &rt, sizeof rt);
}

static int netlink_initial_neigh(struct mnl_socket *nl, lua_State *L)
{
	struct ndmsg rt = { .ndm_family = AF_INET};
	return netlink_initial_generic(nl, L, RTM_GETNEIGH, &rt, sizeof rt);
}

static int bool_from_table(lua_State *L, int idx, const char *item)
{
	int boolean;
	lua_pushstring(L, item);
	lua_gettable(L, idx);
	boolean = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return boolean;
}

static int netlink_event(lua_State *L)
{
	struct mnl_socket *nl;
	int i, ret = 0, wait = 0, all = 0, groups;
	struct {
		const char *name;
		int group;
		int(*initial)(struct mnl_socket *, lua_State *);
	} rtmgrp[] = {
		{ "link",   RTMGRP_LINK,        netlink_initial_link },
		{ "ifaddr", RTMGRP_IPV4_IFADDR, netlink_initial_addr },
		{ "route",  RTMGRP_IPV4_ROUTE,  netlink_initial_route },
		{ "neigh",  RTMGRP_NEIGH,       netlink_initial_neigh },
	};


	if (!lua_isfunction(L, 1))
		luaL_error(L, "Expected callback function");
	if (lua_istable(L, 2)) {
		for (i=0; i< sizeof rtmgrp / sizeof rtmgrp[0]; i++) {
			if (bool_from_table(L, 2, rtmgrp[i].name))
				groups |= rtmgrp[i].group;
			all |= rtmgrp[i].group;
		}
		if (bool_from_table(L, 2, "all"))
			groups = all;
		if (bool_from_table(L, 2, "wait"))
			wait = 1;
	}

	nl = mnl_socket_open(NETLINK_ROUTE);
	if (nl == NULL)
		return luaL_error(L, "mnl_socket_open");

	if (mnl_socket_bind(nl, groups, MNL_SOCKET_AUTOPID) < 0) {
		mnl_socket_close(nl);
		return luaL_error(L, "mnl_socket_bind");
	}
	for (i=0; i< sizeof rtmgrp / sizeof rtmgrp[0] && ret >= 0; i++) {
		if (groups & rtmgrp[i].group)
			ret = rtmgrp[i].initial(nl, L);
	}

	while (ret > 0 && wait) {
		ret = receive(nl, L);
	}
	mnl_socket_close(nl);
	if (ret == -1)
		return luaL_error(L, "mnl_socket_recvfrom()");

	return 0;
}

static const struct luaL_Reg netlink_functions[] = {
	{ "event", netlink_event },
	{ "ethtool", do_ethtool },
	{ NULL, NULL }
};

int luaopen_netlink(lua_State *L)
{
    luaL_newlib(L, netlink_functions);
    return 1;
}
