#!/bin/bash
# Local gtest for the pure upscaler math (WSL Debian, OSG 3.6.5 + gtest)
set -e
cd /mnt/f/Modlists/NerevarMoon/_nm_work/openmw-vfxoffset
out=/tmp/upscalemathtest
g++ -std=c++20 -Wall -Wextra -Werror -I. apps/components_tests/sceneutil/testupscalemath.cpp components/sceneutil/upscalemath.cpp -o $out -lgtest -lgtest_main -losg -pthread
$out "$@"
