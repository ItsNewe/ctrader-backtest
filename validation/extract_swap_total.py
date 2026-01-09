"""
Extract total swap from MT5 Excel reports by summing swap column in deals section
"""
import sys
import zipfile
import xml.etree.ElementTree as ET
import re

def extract_swap_total(filepath):
    """Extract total swap from xlsx by reading XML directly"""
    print(f"\n{'='*70}")
    print(f"Report: {filepath}")
    print('='*70)

    with zipfile.ZipFile(filepath, 'r') as z:
        # Read shared strings
        strings = []
        try:
            with z.open('xl/sharedStrings.xml') as f:
                tree = ET.parse(f)
                root = tree.getroot()
                ns = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'
                for si in root.findall(f'.//{{{ns}}}t'):
                    if si.text:
                        strings.append(si.text)
        except Exception as e:
            print(f"Error reading shared strings: {e}")
            return

        # Find the index of "Swap" header
        swap_header_idx = None
        profit_header_idx = None
        commission_header_idx = None
        for i, s in enumerate(strings):
            if s == "Swap":
                swap_header_idx = i
            if s == "Profit":
                profit_header_idx = i
            if s == "Commission":
                commission_header_idx = i

        print(f"Swap header at string index: {swap_header_idx}")
        print(f"Profit header at string index: {profit_header_idx}")
        print(f"Commission header at string index: {commission_header_idx}")

        # Read worksheet to find column headers and sum swap
        try:
            with z.open('xl/worksheets/sheet1.xml') as f:
                tree = ET.parse(f)
                root = tree.getroot()
                ns = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'

                rows = root.findall(f'.//{{{ns}}}row')
                print(f"Total rows: {len(rows)}")

                # Find the header row with "Deals" section
                header_row = None
                swap_col = None
                profit_col = None
                commission_col = None

                # Track totals
                total_swap = 0.0
                total_profit = 0.0
                total_commission = 0.0
                deal_count = 0

                for row in rows:
                    row_num = int(row.get('r', 0))
                    cells = row.findall(f'{{{ns}}}c')

                    # Build row data
                    cell_values = {}
                    for cell in cells:
                        cell_ref = cell.get('r')  # e.g., "A1", "B2"
                        cell_type = cell.get('t')
                        value_elem = cell.find(f'{{{ns}}}v')

                        if value_elem is not None:
                            col = ''.join(c for c in cell_ref if c.isalpha())
                            if cell_type == 's':
                                idx = int(value_elem.text)
                                if idx < len(strings):
                                    cell_values[col] = strings[idx]
                            else:
                                try:
                                    cell_values[col] = float(value_elem.text)
                                except:
                                    cell_values[col] = value_elem.text

                    # Check if this is a header row with Swap column
                    if swap_col is None:
                        for col, val in cell_values.items():
                            if val == "Swap":
                                swap_col = col
                            if val == "Profit":
                                profit_col = col
                            if val == "Commission":
                                commission_col = col

                        if swap_col:
                            header_row = row_num
                            print(f"Found header row at {row_num}: Swap={swap_col}, Profit={profit_col}, Commission={commission_col}")

                    # If we've passed the header, sum up the swap values
                    elif header_row and row_num > header_row:
                        if swap_col and swap_col in cell_values:
                            try:
                                swap_val = float(cell_values[swap_col])
                                if swap_val != 0:
                                    total_swap += swap_val
                            except:
                                pass

                        if profit_col and profit_col in cell_values:
                            try:
                                profit_val = float(cell_values[profit_col])
                                if profit_val != 0:
                                    total_profit += profit_val
                                    deal_count += 1
                            except:
                                pass

                        if commission_col and commission_col in cell_values:
                            try:
                                comm_val = float(cell_values[commission_col])
                                if comm_val != 0:
                                    total_commission += comm_val
                            except:
                                pass

                print(f"\n--- TOTALS ---")
                print(f"Total Swap:       ${total_swap:,.2f}")
                print(f"Total Profit:     ${total_profit:,.2f}")
                print(f"Total Commission: ${total_commission:,.2f}")
                print(f"Deals counted:    {deal_count}")

                return {
                    'swap': total_swap,
                    'profit': total_profit,
                    'commission': total_commission,
                    'deals': deal_count
                }

        except Exception as e:
            print(f"Error reading worksheet: {e}")
            import traceback
            traceback.print_exc()
            return None

if __name__ == "__main__":
    files = [
        "validation/Broker/fill_up/ReportTester-000000.xlsx",
        "validation/Grid/fill_up/ReportTester-000000.xlsx"
    ]

    results = {}
    for filepath in files:
        try:
            result = extract_swap_total(filepath)
            if result:
                results[filepath] = result
        except Exception as e:
            print(f"Error processing {filepath}: {e}")
            import traceback
            traceback.print_exc()

    # Summary
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)
    for filepath, data in results.items():
        broker = "Broker" if "Broker" in filepath else "Grid"
        print(f"\n{broker}:")
        print(f"  Total Swap:       ${data['swap']:,.2f}")
        print(f"  Total Profit:     ${data['profit']:,.2f}")
        print(f"  Total Commission: ${data['commission']:,.2f}")
        print(f"  Net (Profit+Swap+Comm): ${data['profit'] + data['swap'] + data['commission']:,.2f}")
