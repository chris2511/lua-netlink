nl = require "netlink"
I = require"inspect".inspect
mqtt = require"mqtt"

local x = nl.socket({link = true })
local n = 0

local client = mqtt.client({
	uri = "localhost",
	clean = true,
	id = "netlink",
})

client:start_connecting()
local ifaces = {}

function link_event(links)
  print(I(links))
  for _,link in pairs(links) do
    if not link.name then return end
    for _,k in pairs({ "hwaddr", "up", "running" }) do
      client:publish{
	topic = string.format("netlink/%s/%s",link.name, k),
	payload = tostring(link[k]),
	qos = 1,
	retain = true,
      }
    end
  end
end

link_event(x:query({link=true}))
while true do
  print("### WAIT", n)
  n = n+1
  if x:poll(1000) then
    local e = x:event()
    link_event(e)
  end
end
