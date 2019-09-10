#!/bin/lua

io.write("hello, world\n")

while true do
  io.write("(`q': quit) > ")
  w = io.read()
  if w == 'q' then os.exit() end
  io.write("hello "..w.."\n")
end
