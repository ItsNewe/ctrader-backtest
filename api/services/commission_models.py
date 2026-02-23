"""
Commission model presets and calculation.
"""

COMMISSION_PRESETS = {
    "none": {
        "name": "No Commission",
        "description": "Zero commission (demo accounts)",
        "per_lot": 0.0,
        "model": "fixed",
    },
    "icmarkets_raw": {
        "name": "IC Markets Raw Spread",
        "description": "IC Markets Raw Spread account",
        "per_lot": 3.50,
        "model": "fixed",
    },
    "pepperstone_razor": {
        "name": "Pepperstone Razor",
        "description": "Pepperstone Razor ECN account",
        "per_lot": 3.50,
        "model": "fixed",
    },
    "grid_zero": {
        "name": " Zero",
        "description": " Zero account",
        "per_lot": 2.25,
        "model": "fixed",
    },
    "oanda_core": {
        "name": "OANDA Core Pricing",
        "description": "OANDA Core account",
        "per_lot": 4.00,
        "model": "fixed",
    },
    "interactive_brokers": {
        "name": "Interactive Brokers",
        "description": "IB tiered pricing (approximate flat rate)",
        "per_lot": 2.00,
        "model": "fixed",
    },
    "lmax": {
        "name": "LMAX Exchange",
        "description": "LMAX institutional pricing",
        "per_lot": 2.50,
        "model": "fixed",
    },
    "custom": {
        "name": "Custom",
        "description": "User-defined commission",
        "per_lot": 0.0,
        "model": "fixed",
    },
}


def get_all_presets() -> list:
    """Return all commission presets for the UI."""
    return [{"id": k, **v} for k, v in COMMISSION_PRESETS.items()]


def get_preset(preset_id: str) -> dict:
    """Get a specific preset."""
    return COMMISSION_PRESETS.get(preset_id, COMMISSION_PRESETS["none"])
