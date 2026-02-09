"""
Extract key statistics from MT5 Excel reports
"""
import sys
import zipfile
import xml.etree.ElementTree as ET

def extract_from_xlsx(filepath):
    """Extract data from xlsx by reading XML directly"""
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
            print(f"Found {len(strings)} shared strings")
        except Exception as e:
            print(f"Error reading shared strings: {e}")
            return

        # Search for ALL key financial terms
        print("\n--- Key Financial Terms ---")
        search_terms = ['swap', 'commission', 'profit', 'net', 'total', 'balance',
                        'gross', 'loss', 'trades', 'deals', 'deposit', 'withdrawal']

        found_terms = {}
        for i, s in enumerate(strings):
            s_lower = s.lower()
            for term in search_terms:
                if term in s_lower and len(s) < 100:  # Skip long strings
                    if term not in found_terms:
                        found_terms[term] = []
                    found_terms[term].append((i, s))

        for term in sorted(found_terms.keys()):
            print(f"\n[{term.upper()}]")
            for idx, val in found_terms[term][:10]:  # First 10 matches
                print(f"  [{idx}] {val}")

        # Now read the worksheet to find VALUES
        print("\n--- Reading Worksheet for Values ---")
        try:
            with z.open('xl/worksheets/sheet1.xml') as f:
                tree = ET.parse(f)
                root = tree.getroot()
                ns = 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'

                # Count rows and find summary rows (typically at top)
                rows = root.findall(f'.//{{{ns}}}row')
                print(f"Total rows in worksheet: {len(rows)}")

                # Look for summary section (usually first ~100 rows)
                print("\n--- Summary Section (first 100 rows) ---")
                for row in rows[:100]:
                    row_num = row.get('r')
                    cells = row.findall(f'{{{ns}}}c')

                    row_data = []
                    for cell in cells:
                        cell_ref = cell.get('r')
                        cell_type = cell.get('t')  # 's' = shared string, None = number
                        value_elem = cell.find(f'{{{ns}}}v')

                        if value_elem is not None:
                            if cell_type == 's':
                                # Shared string - look up
                                idx = int(value_elem.text)
                                if idx < len(strings):
                                    row_data.append(strings[idx])
                            else:
                                # Number
                                row_data.append(value_elem.text)

                    # Only print rows with interesting data
                    if row_data:
                        row_str = ' | '.join(str(x) for x in row_data)
                        if any(term in row_str.lower() for term in ['profit', 'swap', 'net', 'total', 'balance', 'trades', 'deals']):
                            print(f"Row {row_num}: {row_str}")

        except Exception as e:
            print(f"Error reading worksheet: {e}")
            import traceback
            traceback.print_exc()

if __name__ == "__main__":
    files = [
        "validation/Broker/fill_up/ReportTester-000000.xlsx",
        "validation/Grid/fill_up/ReportTester-000000.xlsx"
    ]

    for filepath in files:
        try:
            extract_from_xlsx(filepath)
        except Exception as e:
            print(f"Error processing {filepath}: {e}")
            import traceback
            traceback.print_exc()
