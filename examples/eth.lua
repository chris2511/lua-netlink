#!/usr/bin/env lua

nl = require"netlink"
I = require"inspect".inspect

if #arg < 1 then
  print(string.format("usage: %s <interface", arg[0]))
  os.exit(1)
end

print(I(nl.ethtool( { name=arg[1] } )))
