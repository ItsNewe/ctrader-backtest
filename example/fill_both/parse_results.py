import xml.etree.ElementTree as ET
import os
import math

NS = {'ss': 'urn:schemas-microsoft-com:office:spreadsheet'}

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FILES = sorted([f for f in os.listdir(BASE_DIR) if f.endswith('.xml')])


def determine_mode(long_grid: str, short_grid: str) -> str:
    if long_grid == "true" and short_grid == "true":
        return "BOTH"
    elif long_grid == "true" and short_grid == "false":
        return "LONG"
    elif long_grid == "false" and short_grid == "true":
        return "SHORT"
    return "UNKNOWN"


def parse_file(filepath: str):
    tree = ET.parse(filepath)
    root = tree.getroot()

    # Find all Row elements
    rows = root.findall('.//ss:Row', NS)

    # Skip header row (first row)
    data_rows = rows[1:]

    # best[mode] = dict with max profit row data
    best = {}

    for row in data_rows:
        cells = row.findall('ss:Cell', NS)
        if len(cells) < 13:
            continue

        # Extract Data element from each Cell
        def cell_val(idx):
            data_el = cells[idx].find('ss:Data', NS)
            return data_el.text if data_el is not None else None

        def safe_float(val, default=0.0):
            if val is None:
                return default
            try:
                v = float(val)
                return default if math.isnan(v) or math.isinf(v) else v
            except ValueError:
                return default

        profit = safe_float(cell_val(2))
        result = safe_float(cell_val(1))
        spacing = safe_float(cell_val(10))
        trades = int(safe_float(cell_val(9)))
        dd_pct = safe_float(cell_val(8))
        long_grid = cell_val(11)
        short_grid = cell_val(12)

        mode = determine_mode(long_grid, short_grid)
        if mode == "UNKNOWN":
            continue

        if mode not in best or profit > best[mode]['profit']:
            best[mode] = {
                'profit': profit,
                'result': result,
                'spacing': spacing,
                'trades': trades,
                'dd_pct': dd_pct,
            }

    return best


def main():
    all_results = []  # list of (instrument, mode, data_dict)

    for fname in FILES:
        filepath = os.path.join(BASE_DIR, fname)
        instrument = fname.replace('.xml', '')

        if not os.path.exists(filepath):
            print(f"WARNING: {fname} not found, skipping")
            continue

        best = parse_file(filepath)

        for mode in best:
            all_results.append((instrument, mode, best[mode]))

    # Sort: instrument alphabetically, then mode order BOTH, LONG, SHORT
    mode_order = {'BOTH': 0, 'LONG': 1, 'SHORT': 2}
    all_results.sort(key=lambda x: (x[0].upper(), mode_order.get(x[1], 9)))

    # Print formatted table
    header = f"{'Instrument':<14} {'Mode':<6} {'Profit':>14} {'Result':>10} {'Spacing':>8} {'Trades':>7} {'DD%':>8}"
    print(header)
    print("-" * len(header))

    for instrument, mode, d in all_results:
        print(f"{instrument:<14} {mode:<6} {d['profit']:>14,.2f} {d['result']:>10.2f} {d['spacing']:>8.2f} {d['trades']:>7d} {d['dd_pct']:>7.2%}")

    print(f"\nTotal rows: {len(all_results)}")


if __name__ == '__main__':
    main()
