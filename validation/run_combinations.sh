#!/bin/bash
cd "$(dirname "$0")/.."

echo "================================================================================"
echo "FILL-UP BUILDING BLOCKS COMBINATORIC TEST - Gold 2025"
echo "================================================================================"
echo ""
echo "Format: survive spacing hedge_trigger hedge_ratio [dd_pct] [vel_pct]"
echo ""

run_test() {
    local name=$1
    shift
    result=$(./validation/test_fillup_hedged.exe GOLD "$@" 2>&1 | grep -E "Return:|Max Drawdown:|DD Triggers:|Vel Pauses:|Margin Call:")
    ret=$(echo "$result" | grep "Return:" | awk '{print $2}')
    dd=$(echo "$result" | grep "Max Drawdown:" | awk '{print $3}')
    ddtrig=$(echo "$result" | grep "DD Triggers:" | awk '{print $3}')
    velpause=$(echo "$result" | grep "Vel Pauses:" | awk '{print $3}')
    margin=$(echo "$result" | grep "Margin Call:" | awk '{print $3}')
    printf "%-20s %10s %10s %8s %8s %8s\n" "$name" "$ret" "$dd" "$ddtrig" "$velpause" "$margin"
}

printf "%-20s %10s %10s %8s %8s %8s\n" "Config" "Return" "MaxDD" "DDTrig" "VelPause" "Margin"
echo "--------------------------------------------------------------------------------"

echo ""
echo "=== BASELINE (no protection) ==="
run_test "BASE_5"   5  1.0 1.0 0.0
run_test "BASE_6"   6  1.0 1.0 0.0
run_test "BASE_8"   8  1.0 1.0 0.0
run_test "BASE_13" 13  1.0 1.0 0.0

echo ""
echo "=== HEDGE ONLY ==="
run_test "H_6_01"   6  1.0 1.0 0.1
run_test "H_6_02"   6  1.0 1.0 0.2
run_test "H_6_03"   6  1.0 1.0 0.3
run_test "H_8_01"   8  1.0 1.0 0.1
run_test "H_8_02"   8  1.0 1.0 0.2

echo ""
echo "=== DD PROTECTION ONLY ==="
run_test "D_13_50" 13  1.0 1.0 0.0 50
run_test "D_13_60" 13  1.0 1.0 0.0 60
run_test "D_13_70" 13  1.0 1.0 0.0 70
run_test "D_6_50"   6  1.0 1.0 0.0 50
run_test "D_6_60"   6  1.0 1.0 0.0 60
run_test "D_6_70"   6  1.0 1.0 0.0 70
run_test "D_6_80"   6  1.0 1.0 0.0 80

echo ""
echo "=== VELOCITY FILTER ONLY ==="
run_test "V_13_05" 13  1.0 1.0 0.0 0 -0.5
run_test "V_13_10" 13  1.0 1.0 0.0 0 -1.0
run_test "V_13_15" 13  1.0 1.0 0.0 0 -1.5
run_test "V_6_10"   6  1.0 1.0 0.0 0 -1.0

echo ""
echo "=== HEDGE + DD PROTECTION ==="
run_test "HD_6_01_60"  6  1.0 1.0 0.1 60
run_test "HD_6_01_70"  6  1.0 1.0 0.1 70
run_test "HD_6_02_60"  6  1.0 1.0 0.2 60
run_test "HD_8_01_60"  8  1.0 1.0 0.1 60

echo ""
echo "=== HEDGE + VELOCITY ==="
run_test "HV_6_01_10"  6  1.0 1.0 0.1 0 -1.0
run_test "HV_6_02_10"  6  1.0 1.0 0.2 0 -1.0

echo ""
echo "=== DD + VELOCITY ==="
run_test "DV_13_60_10" 13  1.0 1.0 0.0 60 -1.0
run_test "DV_6_60_10"   6  1.0 1.0 0.0 60 -1.0

echo ""
echo "=== TRIPLE COMBO: HEDGE + DD + VELOCITY ==="
run_test "HDV_6_01_60" 6  1.0 1.0 0.1 60 -1.0
run_test "HDV_6_02_60" 6  1.0 1.0 0.2 60 -1.0
run_test "HDV_8_01_60" 8  1.0 1.0 0.1 60 -1.0

echo ""
echo "=== AGGRESSIVE (survive=5%) ==="
run_test "H_5_02"        5  1.0 1.0 0.2
run_test "HD_5_02_60"    5  1.0 1.0 0.2 60
run_test "HDV_5_02_60"   5  1.0 1.0 0.2 60 -1.0

echo ""
echo "================================================================================"
echo "DONE"
echo "================================================================================"
