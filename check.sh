#!/bin/sh

luacheck examples || r=1

cppcheck --enable=all --suppressions-list=CppCheckSuppressions.txt src/*.c || r=1

exit $r
