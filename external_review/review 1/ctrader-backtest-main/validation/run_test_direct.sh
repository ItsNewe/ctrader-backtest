#!/bin/bash
cd "$(dirname "$0")"
./test_fill_up_broker.exe 2>&1 | tee broker_fixed_results.txt
