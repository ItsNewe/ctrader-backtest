"""
Fix FOREX margin calculation in all MQL5 EAs.

Bug: FOREX case uses contract_size/leverage as implicit margin per lot,
     but SYMBOL_MARGIN_INITIAL can override this (e.g. Brent: $1000 vs $2).

Fix: Read SYMBOL_MARGIN_INITIAL in OnInit, substitute when > 0.

This script is idempotent - safe to run multiple times.
"""
import glob, re, os

def fix_file(filepath):
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

    if 'initial_margin_rate' not in content:
        return False
    if 'SYMBOL_CALC_MODE_FOREX' not in content:
        return False

    original = content
    changes = []

    # ── Step 1: Add initial_margin variable declaration ──
    # Skip if already present (idempotency)

    # Pattern A: g_ prefix globals (optimized EAs)
    if 'g_initial_margin_rate' in content and 'g_initial_margin;' not in content:
        pattern = r'(double g_initial_margin_rate,\s*g_maintenance_margin_rate;)'
        match = re.search(pattern, content)
        if match:
            content = content.replace(
                match.group(1),
                match.group(1) + '\ndouble g_initial_margin;  // SYMBOL_MARGIN_INITIAL override'
            )
            changes.append('Added g_initial_margin global')

    # Pattern B: struct members (grid_multi_symbol uses data.initial_margin_rate,
    #            CrossHedgeGrid uses s.initial_margin_rate)
    # Use regex to handle variable spacing (e.g. "double   initial_margin_rate;")
    if re.search(r'double\s+initial_margin_rate\s*;', content) and \
       'double initial_margin;' not in content and 'double   initial_margin;' not in content:
        if 'struct ' in content:
            # Add after the initial_margin_rate member, preserving spacing
            content = re.sub(
                r'(double\s+initial_margin_rate\s*;)',
                r'\1\n    double   initial_margin;  // SYMBOL_MARGIN_INITIAL override',
                content
            )
            changes.append('Added initial_margin to struct')

    # Pattern C: bare globals (GridSimple uses double initial_margin_rate = 0; at file scope)
    if re.search(r'^double initial_margin_rate\s*=\s*0;', content, re.MULTILINE) and \
       'g_initial_margin_rate' not in content and 'double initial_margin;' not in content and \
       'double initial_margin =' not in content:
        pattern = r'(double initial_margin_rate\s*=\s*0;\s*\n\s*double maintenance_margin_rate\s*=\s*0;)'
        match = re.search(pattern, content)
        if match:
            content = content.replace(
                match.group(1),
                match.group(1) + '\ndouble initial_margin = 0;  // SYMBOL_MARGIN_INITIAL override'
            )
            changes.append('Added initial_margin bare global')

    # Pattern D: static locals (fill_up.mq5, fill_up_verbose.mq5, fill_up_with_up_while_up.mq5, etc.)
    # These have: static double initial_margin_rate = 0; inside functions
    if 'static double initial_margin_rate' in content and 'static double initial_margin ' not in content and \
       'g_initial_margin_rate' not in content and 'double initial_margin;' not in content and \
       'double initial_margin =' not in content:
        # Add static double initial_margin = 0; after each static double maintenance_margin_rate = 0;
        content = re.sub(
            r'(static double initial_margin_rate\s*=\s*0;.*?\n\s*static double maintenance_margin_rate\s*=\s*0;[^\n]*)',
            r'\1\n    static double initial_margin = 0;  // SYMBOL_MARGIN_INITIAL override',
            content
        )
        changes.append('Added static initial_margin local')

    # ── Step 2: Add SYMBOL_MARGIN_INITIAL read after SymbolInfoMarginRate ──
    # Use specific check: has the actual function call been added?
    if 'SYMBOL_MARGIN_INITIAL)' not in content:
        # Pattern A: g_ prefix - after SymbolInfoMarginRate with g_ vars
        margin_rate_call = re.search(
            r'(SymbolInfoMarginRate\(_Symbol,\s*ORDER_TYPE_(?:BUY|SELL),\s*g_initial_margin_rate,\s*g_maintenance_margin_rate\);)',
            content
        )
        if margin_rate_call:
            content = content.replace(
                margin_rate_call.group(1),
                margin_rate_call.group(1) + '\n    g_initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);'
            )
            changes.append('Added SYMBOL_MARGIN_INITIAL read (g_ style)')

        # Pattern B: struct-based (grid_multi_symbol uses data., CrossHedgeGrid uses s.)
        struct_margin_calls = list(re.finditer(
            r'(SymbolInfoMarginRate\((\w+),\s*ORDER_TYPE_BUY,\s*(\w+)\.initial_margin_rate,\s*\3\.maintenance_margin_rate\);)',
            content
        ))
        for m in struct_margin_calls:
            content = content.replace(
                m.group(1),
                m.group(1) + f'\n    {m.group(3)}.initial_margin = SymbolInfoDouble({m.group(2)}, SYMBOL_MARGIN_INITIAL);'
            )
            changes.append(f'Added SYMBOL_MARGIN_INITIAL read (struct: {m.group(3)})')

        # Pattern C: bare globals and static locals
        # Match ALL occurrences (some files have 2 functions with SymbolInfoMarginRate)
        bare_margin_calls = list(re.finditer(
            r'(SymbolInfoMarginRate\(_Symbol,\s*ORDER_TYPE_(?:BUY|SELL),\s*initial_margin_rate,\s*maintenance_margin_rate\);)',
            content
        ))
        if bare_margin_calls and 'g_initial_margin_rate' not in content:
            for m in bare_margin_calls:
                content = content.replace(
                    m.group(1),
                    m.group(1) + '\n    initial_margin = SymbolInfoDouble(_Symbol, SYMBOL_MARGIN_INITIAL);',
                    1  # Replace only first occurrence of this exact string
                )
            changes.append(f'Added SYMBOL_MARGIN_INITIAL read (bare style, {len(bare_margin_calls)} calls)')

    # ── Step 3: Fix FOREX simplified patterns ──
    # Pattern: "* contract_size / leverage * initial_margin_rate"
    # Replace: "* (initial_margin > 0 ? initial_margin : contract_size / leverage) * initial_margin_rate"
    # These are already idempotent (regex won't match after fix)

    # g_ prefix version
    content = re.sub(
        r'(\*\s*)g_contract_size\s*/\s*g_leverage\s*\*\s*g_initial_margin_rate',
        r'\1(g_initial_margin > 0 ? g_initial_margin : g_contract_size / g_leverage) * g_initial_margin_rate',
        content
    )

    # struct s. version: s.contract_size / s.leverage * s.initial_margin_rate
    content = re.sub(
        r'(\*\s*)s\.contract_size\s*/\s*s\.leverage\s*\*\s*s\.initial_margin_rate',
        r'\1(s.initial_margin > 0 ? s.initial_margin : s.contract_size / s.leverage) * s.initial_margin_rate',
        content
    )

    # bare: contract_size / leverage * initial_margin_rate (no prefix)
    # Be careful not to match g_ or s. versions
    content = re.sub(
        r'(\*\s*)contract_size\s*/\s*leverage\s*\*\s*initial_margin_rate(?!\w)',
        r'\1(initial_margin > 0 ? initial_margin : contract_size / leverage) * initial_margin_rate',
        content
    )

    # ── Step 4: Fix FOREX complex formula patterns ──
    # In the denominator: "initial_margin_rate * margin_stop_out_level"
    # becomes: "(initial_margin > 0 ? initial_margin * leverage / contract_size : 1.0) * initial_margin_rate * margin_stop_out_level"
    # BUT only within FOREX case blocks, not FOREX_NO_LEVERAGE or CFDLEVERAGE

    lines = content.split('\n')
    new_lines = []
    in_forex_case = False
    in_forex_nolev_case = False

    for i, line in enumerate(lines):
        stripped = line.strip()

        # Track which case we're in
        if 'case SYMBOL_CALC_MODE_FOREX:' in stripped and 'NO_LEVERAGE' not in stripped:
            in_forex_case = True
            in_forex_nolev_case = False
        elif 'case SYMBOL_CALC_MODE_FOREX_NO_LEVERAGE:' in stripped:
            in_forex_nolev_case = True
            in_forex_case = False
        elif stripped.startswith('case ') or stripped.startswith('default:'):
            in_forex_case = False
            in_forex_nolev_case = False

        # IDEMPOTENCY: Only apply complex formula fix if not already applied
        already_fixed = 'initial_margin > 0' in line

        # Fix complex formula in FOREX case
        if in_forex_case and not already_fixed:
            # g_ prefix: g_initial_margin_rate * g_margin_stop_out_level
            line = re.sub(
                r'g_initial_margin_rate\s*\*\s*(g_margin_stop_out_level|margin_stop_out_level)',
                r'(g_initial_margin > 0 ? g_initial_margin * g_leverage / g_contract_size : 1.0) * g_initial_margin_rate * \1',
                line
            )
            # data. prefix: data.initial_margin_rate * g_margin_stop_out_level
            line = re.sub(
                r'data\.initial_margin_rate\s*\*\s*g_margin_stop_out_level',
                r'(data.initial_margin > 0 ? data.initial_margin * g_leverage / data.contract_size : 1.0) * data.initial_margin_rate * g_margin_stop_out_level',
                line
            )
            # bare: initial_margin_rate * margin_stop_out_level (no g_ or data. prefix)
            if 'g_initial_margin_rate' not in line and 'data.initial_margin_rate' not in line:
                line = re.sub(
                    r'initial_margin_rate\s*\*\s*margin_stop_out_level',
                    r'(initial_margin > 0 ? initial_margin * leverage / contract_size : 1.0) * initial_margin_rate * margin_stop_out_level',
                    line
                )

        # Fix complex formula in FOREX_NO_LEVERAGE case
        if in_forex_nolev_case and not already_fixed:
            # g_ prefix
            line = re.sub(
                r'g_initial_margin_rate\s*\*\s*(g_margin_stop_out_level|margin_stop_out_level)',
                r'(g_initial_margin > 0 ? g_initial_margin / g_contract_size : 1.0) * g_initial_margin_rate * \1',
                line
            )
            # data. prefix: data.initial_margin_rate * g_margin_stop_out_level
            line = re.sub(
                r'data\.initial_margin_rate\s*\*\s*g_margin_stop_out_level',
                r'(data.initial_margin > 0 ? data.initial_margin / data.contract_size : 1.0) * data.initial_margin_rate * g_margin_stop_out_level',
                line
            )
            # bare (no g_ or data. prefix)
            if 'g_initial_margin_rate' not in line and 'data.initial_margin_rate' not in line:
                line = re.sub(
                    r'initial_margin_rate\s*\*\s*margin_stop_out_level',
                    r'(initial_margin > 0 ? initial_margin / contract_size : 1.0) * initial_margin_rate * margin_stop_out_level',
                    line
                )

        # Fix FOREX_NO_LEVERAGE simplified patterns (mode-aware)
        # Pattern: "* contract_size * initial_margin_rate" (no /leverage)
        # Replace: "* (initial_margin > 0 ? initial_margin : contract_size) * initial_margin_rate"
        if in_forex_nolev_case and not already_fixed:
            # s. prefix: s.contract_size * s.initial_margin_rate
            line = re.sub(
                r'(\*\s*)s\.contract_size\s*\*\s*s\.initial_margin_rate',
                r'\1(s.initial_margin > 0 ? s.initial_margin : s.contract_size) * s.initial_margin_rate',
                line
            )
            # bare: contract_size * initial_margin_rate (ensure not preceded by g_ or s. or data.)
            if 'g_contract_size' not in line and 's.contract_size' not in line and 'data.contract_size' not in line:
                line = re.sub(
                    r'(\*\s*)contract_size\s*\*\s*initial_margin_rate(?!\w)',
                    r'\1(initial_margin > 0 ? initial_margin : contract_size) * initial_margin_rate',
                    line
                )

        new_lines.append(line)

    content = '\n'.join(new_lines)

    if content != original:
        with open(filepath, 'w', encoding='utf-8', newline='') as f:
            f.write(content)
        return True
    return False

# Find all MQL5 files
files = []
for pattern in ['example/**/*.mq5', 'mt5/**/*.mq5']:
    files.extend(glob.glob(pattern, recursive=True))

fixed = []
skipped = []
for f in sorted(set(files)):
    if fix_file(f):
        fixed.append(f)
    else:
        skipped.append(f)

print(f"Fixed {len(fixed)} files:")
for f in fixed:
    print(f"  + {f}")
print(f"\nSkipped {len(skipped)} files (no FOREX case or no margin_rate):")
for f in skipped:
    print(f"  - {f}")
