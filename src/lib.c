/*
 * Copyright (c) 2021 Christian Hohnstaedt
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <arpa/inet.h>
#include <libmnl/libmnl.h>
#include <linux/rtnetlink.h>

#include "netlink.h"

void push_string(lua_State *L, const char *which, const char *value)
{
	lua_pushstring(L, which);
	lua_pushstring(L, value);
	lua_settable(L, -3);
}

void push_integer(lua_State *L, const char *which, lua_Integer value)
{
	lua_pushstring(L, which);
	lua_pushinteger(L, value);
	lua_settable(L, -3);
}

void push_u32_attr(lua_State *L, const char *which, const struct nlattr *attr)
{
	uint32_t *payload = mnl_attr_get_payload(attr);
	push_integer(L, which, *payload);
}

void push_bool(lua_State *L, const char *which, int value)
{
	push_string(L, which, value ? "yes" : "no");
}

void push_ip(lua_State *L, const char *which, int family,
			const struct nlattr *attr)
{
	char buf[INET6_ADDRSTRLEN];
	inet_ntop(family, mnl_attr_get_payload(attr), buf, sizeof buf);
	push_string(L, which, buf);
}

void push_cidr(lua_State *L, const char *which, int family,
			const struct nlattr *attr, int cidr)
{
	char buf[INET6_ADDRSTRLEN];
	inet_ntop(family, mnl_attr_get_payload(attr), buf, sizeof buf);
	lua_pushstring(L, which);
	lua_pushfstring(L, "%s/%d", buf, cidr);
	lua_settable(L, -3);
}

void push_hwaddr(lua_State *L, const char *which,
			const struct nlattr *attr)
{
	uint8_t *hwaddr = mnl_attr_get_payload(attr);
	char addr[20], *p = addr;
	int i, max = mnl_attr_get_payload_len(attr);

	if (mnl_attr_validate(attr, MNL_TYPE_BINARY) < 0)
		luaL_error(L, "Invalid mnl_attr_type %d for hardware address",
				(int)mnl_attr_get_type(attr));
	for (i = 0; i < max; i++) {
		p += sprintf(p, "%02x", hwaddr[i]);
		if (i + 1 != max)
			*p++ = ':';
	}
	push_string(L, which, addr);
}

int netlink_initial(struct mnl_socket *nl, lua_State *L, int type)
{
	struct rtgenmsg rt = { .rtgen_family = AF_INET };
	char buf[MNL_SOCKET_BUFFER_SIZE];
	struct nlmsghdr *nlh;
	void *dst;

	nlh = mnl_nlmsg_put_header(buf);
	nlh->nlmsg_type	= type;
	nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	nlh->nlmsg_seq = 0;
	dst = mnl_nlmsg_put_extra_header(nlh, sizeof rt);
	memcpy(dst, &rt, sizeof rt);

	if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0)
		return -1;

	return receive(nl, L);
}
