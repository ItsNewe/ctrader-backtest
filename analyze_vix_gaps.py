"""
VIX Tick Data - Overnight Gap & Rollover Analysis
Reads tab-delimited tick data, computes day-over-day gaps,
identifies rollover gaps around 3rd Wednesdays.
"""
import csv
from datetime import datetime, timedelta
from collections import OrderedDict

FILE = r"C:\Users\user\Documents\ctrader-backtest\validation\Broker\VIX_TICKS_FULL.csv"

# 1. Read tick data, extract first/last mid-price per day
print("Reading VIX tick data...")
day_first = OrderedDict()
day_last  = OrderedDict()

with open(FILE, "r") as f:
    reader = csv.reader(f, delimiter="\t")
    header = next(reader)
    for row in reader:
        ts_str = row[0]
        bid = float(row[1])
        ask = float(row[2])
        mid = (bid + ask) / 2.0
        date_str = ts_str[:10]
        if date_str not in day_first:
            day_first[date_str] = (ts_str, mid)
        day_last[date_str] = (ts_str, mid)

dates = list(day_first.keys())
print(f"  Total trading days: {len(dates)}")
print(f"  Date range: {dates[0]} to {dates[-1]}")

# 2. Compute overnight gaps
gaps = []
for i in range(1, len(dates)):
    prev_date = dates[i - 1]
    curr_date = dates[i]
    prev_last_ts, prev_last_mid = day_last[prev_date]
    curr_first_ts, curr_first_mid = day_first[curr_date]
    gap = curr_first_mid - prev_last_mid
    gap_pct = (gap / prev_last_mid) * 100.0
    gaps.append({
        "date": curr_date,
        "prev_date": prev_date,
        "prev_close": prev_last_mid,
        "curr_open": curr_first_mid,
        "gap": gap,
        "gap_pct": gap_pct,
        "prev_close_ts": prev_last_ts,
        "curr_open_ts": curr_first_ts,
    })

# 3. Top 20 largest absolute gaps
print("\n" + "=" * 90)
print("TOP 20 LARGEST ABSOLUTE OVERNIGHT GAPS")
print("=" * 90)
sorted_by_abs = sorted(gaps, key=lambda g: abs(g["gap"]), reverse=True)
print(f"{'#':>3}  {'Date':>12}  {'Prev Close':>11}  {'Open':>11}  {'Gap':>8}  {'Gap%':>7}  Direction")
print("-" * 90)
for i, g in enumerate(sorted_by_abs[:20]):
    direction = "UP" if g["gap"] > 0 else "DOWN"
    print(f"{i+1:3d}  {g['date']:>12}  {g['prev_close']:11.3f}  {g['curr_open']:11.3f}  "
          f"{g['gap']:+8.3f}  {g['gap_pct']:+6.2f}%  {direction}")

# 4. Top 10 gap-up and gap-down separately
print("\n" + "=" * 90)
print("TOP 10 GAP-UP (contango roll / fear spike)")
print("=" * 90)
sorted_up = sorted(gaps, key=lambda g: g["gap"], reverse=True)
print(f"{'#':>3}  {'Date':>12}  {'Prev Close':>11}  {'Open':>11}  {'Gap':>8}  {'Gap%':>7}")
print("-" * 80)
for i, g in enumerate(sorted_up[:10]):
    print(f"{i+1:3d}  {g['date']:>12}  {g['prev_close']:11.3f}  {g['curr_open']:11.3f}  "
          f"{g['gap']:+8.3f}  {g['gap_pct']:+6.2f}%")

print(f"\nTOP 10 GAP-DOWN (backwardation roll / relief)")
print("=" * 90)
sorted_down = sorted(gaps, key=lambda g: g["gap"])
print(f"{'#':>3}  {'Date':>12}  {'Prev Close':>11}  {'Open':>11}  {'Gap':>8}  {'Gap%':>7}")
print("-" * 80)
for i, g in enumerate(sorted_down[:10]):
    print(f"{i+1:3d}  {g['date']:>12}  {g['prev_close']:11.3f}  {g['curr_open']:11.3f}  "
          f"{g['gap']:+8.3f}  {g['gap_pct']:+6.2f}%")

# 5. Known VIX rollover dates (3rd Wednesday of each month)
def third_wednesday(year, month):
    first = datetime(year, month, 1)
    days_to_wed = (2 - first.weekday()) % 7
    first_wed = first + timedelta(days=days_to_wed)
    third_wed = first_wed + timedelta(weeks=2)
    return third_wed

rollover_dates = []
for year in [2024, 2025, 2026]:
    for month in range(1, 13):
        if year == 2026 and month > 2:
            break
        rd = third_wednesday(year, month)
        rollover_dates.append(rd.strftime("%Y.%m.%d"))

print("\n" + "=" * 90)
print("GAPS AROUND VIX FUTURES ROLLOVER DATES (3rd Wednesday of each month)")
print("Showing gaps on rollover day and +/- 1 trading day")
print("=" * 90)

gap_by_date = {g["date"]: g for g in gaps}

print(f"{'Rollover Date':>14}  {'Nearby Gap Date':>14}  {'Prev Close':>11}  {'Open':>11}  {'Gap':>8}  {'Gap%':>7}")
print("-" * 90)

rollover_gap_sum = 0.0
rollover_gap_count = 0

for rd in rollover_dates:
    rd_dt = datetime.strptime(rd, "%Y.%m.%d")
    found = False
    for offset in range(-1, 3):
        check_dt = rd_dt + timedelta(days=offset)
        check_str = check_dt.strftime("%Y.%m.%d")
        if check_str in gap_by_date:
            g = gap_by_date[check_str]
            marker = " <-- ROLL" if offset in [0, 1] else ""
            print(f"{rd:>14}  {check_str:>14}  {g['prev_close']:11.3f}  {g['curr_open']:11.3f}  "
                  f"{g['gap']:+8.3f}  {g['gap_pct']:+6.2f}%{marker}")
            if offset in [0, 1]:
                rollover_gap_sum += g["gap"]
                rollover_gap_count += 1
            found = True
    if found:
        print()

print(f"\nRollover-day gaps counted: {rollover_gap_count}")
print(f"Sum of rollover-day gaps: {rollover_gap_sum:+.3f}")
print(f"Avg rollover-day gap:     {rollover_gap_sum / max(1, rollover_gap_count):+.3f}")

# 6. Cumulative overnight gap (contango drag)
print("\n" + "=" * 90)
print("CUMULATIVE OVERNIGHT GAP (Net Contango Drag)")
print("=" * 90)

cumulative = 0.0
monthly_gaps = OrderedDict()

for g in gaps:
    cumulative += g["gap"]
    month_key = g["date"][:7]
    if month_key not in monthly_gaps:
        monthly_gaps[month_key] = 0.0
    monthly_gaps[month_key] += g["gap"]

print(f"\n{'Month':>10}  {'Monthly Gap Sum':>15}  {'Cumulative':>12}")
print("-" * 45)
running = 0.0
for month, mgap in monthly_gaps.items():
    running += mgap
    print(f"{month:>10}  {mgap:+15.3f}  {running:+12.3f}")

print(f"\n{'TOTAL CUMULATIVE OVERNIGHT GAP':>35}: {cumulative:+.3f}")
print(f"{'Total trading days':>35}: {len(gaps)}")
print(f"{'Average overnight gap':>35}: {cumulative / len(gaps):+.4f}")
print(f"{'Median overnight gap':>35}: ", end="")

all_gaps_sorted = sorted([g["gap"] for g in gaps])
n = len(all_gaps_sorted)
median = (all_gaps_sorted[n // 2] + all_gaps_sorted[(n - 1) // 2]) / 2.0
print(f"{median:+.4f}")

pos_gaps = [g["gap"] for g in gaps if g["gap"] > 0]
neg_gaps = [g["gap"] for g in gaps if g["gap"] < 0]
zero_gaps = [g["gap"] for g in gaps if g["gap"] == 0]
print(f"\n{'Gap-up days':>35}: {len(pos_gaps)} ({100*len(pos_gaps)/len(gaps):.1f}%)")
print(f"{'Gap-down days':>35}: {len(neg_gaps)} ({100*len(neg_gaps)/len(gaps):.1f}%)")
print(f"{'Zero-gap days':>35}: {len(zero_gaps)} ({100*len(zero_gaps)/len(gaps):.1f}%)")
print(f"{'Avg gap-up size':>35}: {sum(pos_gaps)/max(1,len(pos_gaps)):+.4f}")
print(f"{'Avg gap-down size':>35}: {sum(neg_gaps)/max(1,len(neg_gaps)):+.4f}")

# 7. Day-of-week breakdown
print("\n" + "=" * 90)
print("DAY-OF-WEEK GAP BREAKDOWN")
print("=" * 90)

dow_names = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
dow_gaps = {i: [] for i in range(7)}

for g in gaps:
    dt = datetime.strptime(g["date"], "%Y.%m.%d")
    dow_gaps[dt.weekday()].append(g["gap"])

print(f"{'Day':>5}  {'Count':>6}  {'Avg Gap':>9}  {'Sum Gap':>10}  {'Avg |Gap|':>10}")
print("-" * 50)
for dow in range(7):
    gl = dow_gaps[dow]
    if gl:
        avg = sum(gl) / len(gl)
        total = sum(gl)
        avg_abs = sum(abs(x) for x in gl) / len(gl)
        print(f"{dow_names[dow]:>5}  {len(gl):6d}  {avg:+9.4f}  {total:+10.3f}  {avg_abs:10.4f}")

print("\nDone.")
