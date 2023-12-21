#!/usr/bin/env lua

local nl = require"netlink"
local I = require"inspect"

print(string.format("All known netlink groups: %s",
			table.concat(nl.groups(), ", ")))

local nls = nl.socket()

print("File descriptor:", nls:fd())

local links = nls:query{ link = true }
print("Query link status:", I(links))

for _, entry in pairs(links) do
  local eth = nl.ethtool(entry.name)
  if eth.speed then
    print("ETH:", entry.name, I(eth))
  end
end

print("GROUPS:", I(nls:groups()))
while true do
  print("WAIT for any event")
  nls:poll()
  print(I(nls:event()))
end
