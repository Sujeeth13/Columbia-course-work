#!/bin/bash

echo "---------------A1---------------"
./test_simple 2>/dev/null
echo "---------------A2---------------"
./test_robust 2>/dev/null
echo "---------------A3---------------"
chmod +x clear_cache.sh
./clear_cache.sh
./test_basic 2>/dev/null
echo "---------------A4---------------"
./test_get_wait 2>/dev/null
./test_get_wait_multi 2>/dev/null
echo "---------------A4 Second Time---------------"
./test_get_wait 2>/dev/null
./test_get_wait_multi 2>/dev/null
echo "---------------A5---------------"
./test_clear_wakeup 2>/dev/null
echo "---------------A6---------------"
./test_pressure 2>/dev/null
