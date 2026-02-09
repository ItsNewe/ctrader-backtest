"""Analyze swap from MT5 SQL log"""
import sys

prev_swap = 0
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    for line in f:
        if 'DAY_CHANGE' not in line:
            continue
        parts = line.strip().split(',')
        date = parts[1]
        lots = float(parts[7])
        cumul_swap = float(parts[16])
        day = parts[17]

        daily = cumul_swap - prev_swap
        per_lot = daily / lots if lots > 0 else 0

        # Expected: -65.11 * 0.01 * 100 = -65.11 per lot (1x)
        expected_1x = -65.11
        ratio = per_lot / expected_1x if expected_1x != 0 else 0

        marker = '*** TRIPLE' if ratio > 2.5 else ''
        print(f'{date} {day}: lots={lots:.2f}, daily={daily:8.2f}, per_lot={per_lot:8.2f}, ratio={ratio:.2f}x {marker}')
        prev_swap = cumul_swap
