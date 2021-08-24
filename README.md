# Netlink Lua

The lua library `netlink.so` uses the netlink socket to acquire
network state information about interface links, IP addresses, routes
and address resolutions

It additionally allows to listen for netlink events and receive changes.
Usage examples can be found in the examples directory.

## Examples

```
> require"netlink".ethtool("eno1").speed
100
> require"netlink".ethtool("eno1").duplex
full
> require"inspect".inspect(require"netlink".socket():query({route=true}))
{ {
    event = "newroute",
    gateway = "192.168.178.1",
    scope = 0,
    stamp = 252071566
  }, {
    dst = "192.168.178.0/24",
    event = "newroute",
    scope = 253,
    stamp = 252071566
  } }
```

```
> s = require"netlink".socket()
> s:poll()
true
> s:event()
{ {
    event = "newlink",
    hwaddr = "ce:d4:ad:5a:e6:24",
    index = 5,
    mtu = 1500,
    name = "dummy0",
    running = false,
    stamp = 252811866,
    up = false
  } }
```

## Functions of the netlink library

### netlink.ethtool() function

Returns ethernet information about the given interface name.
If an array is provided as argument, it is expected to contain an entry
called `name` with the interface name.

If the interface is an ethernet interface,
the table is filled with the following values:

 - speed (10 / 100 / 1000)
 - duplex (full / half)
 - autoneg (true / false)
 - name (interface name)

### netlink.groups() function

Returns an array of strings of all known groups.
```
> require"inspect".inspect(require"netlink".groups())
{ "link", "ifaddr", "route", "neigh" }
```

### netlink.socket() function

The `netlink.socket()` function returns a table to handle netlink events.
The optional parameter is a set of netlink groups returned
by `netlink.groups()`:
```
local s = require"netlink".socket( { link = true, ifaddr = true } )
```
If unset, all supported groups will be used.

#### Methods of the netlink socket class

The returned table contains the entry "\_mnl\_userdata" which contains
the netlink data. It additionally comes with the following methods:

 - fd() Returns the file descriptor to be used in luaposix.poll()
 - event() Returns an array of dictionaries with changed items
 - query() Triggers all events, registered with netlink.socket().
 - groups() Returns an array of strings of all registered groups to
     receive events for.
 - poll() Since events() does not block and in case of no events immediately
     returns an empty array, poll() can be used to wait for new events.

### Returned netlink data

All returned tables have the following entries:

 - event: new/del + group name, e.g. newlink, delifaddr
 - stamp: milliseconds since boot (CLOCK\_MONOTONIC)
 - index: Interface index

#### Event "newlink" and "dellink"

 - hwaddr: The MAC address
 - mtu: Maximum Transfer Unit
 - name: Interface name
 - up: Reflects the administrative state of the interface (IFF\_UP)
 - running: Reflects the operational state (IFF\_RUNNING).

#### Event "newifaddr" and "delifaddr"

 - family: AF\_INET or AF\_INET6
 - ip: IP address

#### Event "newroute" and "delroute"

 - scope: Sort of distance to the destination.
 - gateway: Gateway IP (For scope 0)
 - dst: Network (For scope 253)

#### Event "newneigh", and "delneigh"

 - hwaddr: The resolved MAC address associated with the requested IP address
 - ip: Requested IP address
 - state: One out of: "reachable", "stale", "failed"
 - probes: Number of probes

