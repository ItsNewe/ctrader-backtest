"""
Analyze Test E: Swap Timing
Extracts swap application timing patterns
"""

import pandas as pd
from pathlib import Path
import json
from datetime import datetime

def analyze_swap_timing():
    """Analyze swap timing data from MT5"""

    print("=" * 70)
    print("TEST E: SWAP TIMING ANALYSIS")
    print("=" * 70)
    print()

    # Load data
    data_file = Path("validation/mt5/test_e_swap_timing.csv")
    summary_file = Path("validation/mt5/test_e_summary.json")

    if not data_file.exists():
        print(f"ERROR: {data_file} not found")
        return False

    df = pd.read_csv(data_file, sep='\t')

    with open(summary_file) as f:
        summary = json.load(f)

    print(f"Monitoring duration: {summary['duration_hours']} hours")
    print(f"Swap events detected: {summary['swap_events_detected']}")
    print()

    # Filter swap events
    swap_events = df[df['event'] == 'swap_applied']

    if len(swap_events) == 0:
        print("WARNING: No swap events detected")
        print("This might mean:")
        print("  - Test duration was too short")
        print("  - Weekend period (no swap)")
        print("  - Swap-free account")
        return False

    print("SWAP EVENTS DETECTED")
    print("-" * 70)
    print(f"{'Timestamp':<25} {'Day of Week':<12} {'Swap Change':<15} {'Balance':<15}")
    print("-" * 70)

    for _, event in swap_events.iterrows():
        print(f"{event['time_str']:<25} {event['day_of_week']:<12} ${event['swap_change']:<14.2f} ${event['balance']:<14.2f}")

    print()

    # Analyze timing patterns
    print("TIMING PATTERN ANALYSIS")
    print("-" * 70)

    # Extract hour and day from timestamps
    for idx, event in swap_events.iterrows():
        time_str = event['time_str']
        # Parse time (format: "YYYY.MM.DD HH:MM")
        try:
            dt = datetime.strptime(time_str, "%Y.%m.%d %H:%M")
            print(f"Swap applied at: {dt.strftime('%A, %H:%M')} (Hour: {dt.hour})")
        except:
            print(f"Could not parse time: {time_str}")

    print()

    # Check for Wednesday triple swap
    days_with_swap = swap_events['day_of_week'].value_counts()
    print("SWAP EVENTS BY DAY OF WEEK")
    print("-" * 70)
    for day, count in days_with_swap.items():
        marker = " ⚠ (Triple swap?)" if day == "Wednesday" else ""
        print(f"  {day}: {count} event(s){marker}")

    print()

    # Implementation recommendations
    print("IMPLEMENTATION RECOMMENDATIONS")
    print("=" * 70)
    print()

    # Determine swap time from events
    swap_hours = []
    for _, event in swap_events.iterrows():
        try:
            dt = datetime.strptime(event['time_str'], "%Y.%m.%d %H:%M")
            swap_hours.append(dt.hour)
        except:
            pass

    if swap_hours:
        common_hour = max(set(swap_hours), key=swap_hours.count)
        print(f"Swap typically applied at: {common_hour:02d}:00 server time")
    else:
        common_hour = 0
        print("Could not determine swap time (defaulting to 00:00)")

    print()
    print("For our C++ engine, implement swap timing as:")
    print()
    print("```cpp")
    print("// In include/swap_manager.h")
    print("class SwapManager {")
    print("private:")
    print(f"    const int swap_hour_ = {common_hour};  // Server time hour for swap")
    print("    datetime last_swap_check_ = 0;")
    print()
    print("public:")
    print("    bool ShouldApplySwap(datetime current_time) {")
    print("        MqlDateTime dt;")
    print("        TimeToStruct(current_time, dt);")
    print("        ")
    print("        // Check if we've crossed the swap hour")
    print("        if (dt.hour >= swap_hour_ && ")
    print("            TimeDayOfYear(current_time) != TimeDayOfYear(last_swap_check_)) {")
    print("            last_swap_check_ = current_time;")
    print("            return true;")
    print("        }")
    print("        return false;")
    print("    }")
    print()
    print("    double CalculateSwap(")
    print("        double lot_size,")
    print("        bool is_buy,")
    print("        double swap_long,   // From symbol info")
    print("        double swap_short,  // From symbol info")
    print("        int day_of_week")
    print("    ) {")
    print("        double swap_points = is_buy ? swap_long : swap_short;")
    print("        ")
    print("        // Triple swap on Wednesday (for weekend)")
    print("        if (day_of_week == 3) {  // Wednesday")
    print("            swap_points *= 3;")
    print("        }")
    print("        ")
    print("        return lot_size * swap_points;")
    print("    }")
    print("};")
    print("```")
    print()

    # Export analysis
    analysis = {
        'test': 'Test E - Swap Timing',
        'swap_events_detected': len(swap_events),
        'monitoring_duration_hours': summary['duration_hours'],
        'swap_hour': int(common_hour) if swap_hours else 0,
        'events': []
    }

    for _, event in swap_events.iterrows():
        try:
            dt = datetime.strptime(event['time_str'], "%Y.%m.%d %H:%M")
            analysis['events'].append({
                'timestamp': event['time_str'],
                'day_of_week': event['day_of_week'],
                'hour': dt.hour,
                'swap_change': float(event['swap_change']),
                'balance': float(event['balance'])
            })
        except:
            pass

    output_file = Path("validation/analysis/test_e_analysis.json")
    output_file.parent.mkdir(parents=True, exist_ok=True)

    with open(output_file, 'w') as f:
        json.dump(analysis, f, indent=2)

    print(f"Analysis saved to: {output_file}")
    print()

    return True

if __name__ == "__main__":
    import sys
    success = analyze_swap_timing()
    sys.exit(0 if success else 1)
