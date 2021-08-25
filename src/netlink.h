/*
 * Copyright (c) 2021 Christian Hohnstaedt
 * SPDX-License-Identifier: MIT
 */

#ifndef NETLINK_LUA_H_
#define NETLINK_LUA_H_

#include <lua.h>

#define MNL_META_NAME "_mnl_meta_table"
#define MNL_META_CLASS "_mnl_meta_class"
#define MNL_USERDATA "_mnl_userdata"

#define TRACE printf("STACK[%d]: %d, top: %s\n", __LINE__,\
			lua_gettop(L), luaL_typename(L, -1));

#define EXPORT_SYMBOL(x) typeof(x) (x) __attribute__((visibility("default")))

struct rtmsg;
struct ifinfomsg;
struct ifaddrmsg;
struct ndmsg;
struct nlattr;
struct nlmsghdr;

struct callback_data {
	lua_State *L;
	union {
		struct rtmsg *rtm;
		struct ifinfomsg *ifm;
		struct ifaddrmsg *ifa;
		struct ndmsg *ndm;
		void *nl_payload;
	};
};

struct userdata {
	struct mnl_socket *nl;
	int groups;
};

struct rtmgrp {
	const char *name;
	int group;
	int (*callback) (const struct nlmsghdr *, struct callback_data *);
	int new;
	int del;
	int get;
};

#define LUA_RTMGRP(x) \
 struct rtmgrp (x) __attribute__ ((section ("rtmgrp"), aligned(sizeof(void*))))

extern struct rtmgrp __start_rtmgrp;
extern struct rtmgrp __stop_rtmgrp;

int luaopen_netlink(lua_State *L);
int netlink_ethtool(lua_State *L);

void push_ip(lua_State *L, const char *which, int family,
		const struct nlattr *attr);
void push_cidr(lua_State *L, const char *which, int family,
		const struct nlattr *attr, int cidr);
void push_hwaddr(lua_State *L, const char *which,
		const struct nlattr *attr);

int netlink_initial(struct mnl_socket *nl, lua_State *L, int type);
int receive(struct mnl_socket *nl, lua_State *L);

#endif
