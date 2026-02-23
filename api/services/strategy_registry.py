"""
Strategy metadata registry.
Maps strategy names to their parameters, defaults, and CLI identifiers.
Must match the C++ Config structs exactly.
"""

STRATEGY_REGISTRY = {
    "FillUpOscillation": {
        "name": "FillUp Oscillation",
        "description": "Adaptive grid trading with volatility-based spacing",
        "cli_name": "fillup",
        "parameters": [
            {
                "name": "survive_pct",
                "label": "Survive %",
                "type": "float",
                "default": 13.0,
                "min": 1.0,
                "max": 50.0,
                "step": 0.5,
                "description": "Price drop percentage the strategy must survive",
            },
            {
                "name": "base_spacing",
                "label": "Base Spacing ($)",
                "type": "float",
                "default": 1.50,
                "min": 0.1,
                "max": 20.0,
                "step": 0.1,
                "description": "Base distance between grid levels",
            },
            {
                "name": "mode",
                "label": "Mode",
                "type": "select",
                "default": "ADAPTIVE_SPACING",
                "options": [
                    "BASELINE",
                    "ADAPTIVE_SPACING",
                    "ANTIFRAGILE",
                    "VELOCITY_FILTER",
                    "ALL_COMBINED",
                ],
                "description": "Spacing adaptation mode",
            },
            {
                "name": "lookback_hours",
                "label": "Volatility Lookback (hours)",
                "type": "float",
                "default": 4.0,
                "min": 0.5,
                "max": 48.0,
                "step": 0.5,
                "description": "Hours of price history for volatility calculation",
            },
            {
                "name": "antifragile_scale",
                "label": "Antifragile Scale",
                "type": "float",
                "default": 0.1,
                "min": 0.01,
                "max": 1.0,
                "step": 0.01,
                "description": "Scale factor for antifragile spacing adjustment",
            },
            {
                "name": "velocity_threshold",
                "label": "Velocity Threshold ($/hr)",
                "type": "float",
                "default": 30.0,
                "min": 1.0,
                "max": 200.0,
                "step": 1.0,
                "description": "Price velocity threshold to pause trading",
            },
            {
                "name": "max_spacing_mult",
                "label": "Max Spacing Multiplier",
                "type": "float",
                "default": 30.0,
                "min": 1.0,
                "max": 100.0,
                "step": 1.0,
                "description": "Maximum adaptive spacing multiplier",
            },
            {
                "name": "pct_spacing",
                "label": "Percentage Spacing",
                "type": "bool",
                "default": False,
                "description": "Use percentage-based spacing (required for XAGUSD)",
            },
            {
                "name": "force_min_volume_entry",
                "label": "Force Min Volume Entry",
                "type": "bool",
                "default": False,
                "description": "Always enter with min volume (safer for crashes)",
            },
        ],
    },
    "CombinedJu": {
        "name": "Combined Ju",
        "description": "Highest returns strategy: Rubber Band TP + Velocity Filter + Barbell Sizing",
        "cli_name": "combined",
        "parameters": [
            {
                "name": "survive_pct",
                "label": "Survive %",
                "type": "float",
                "default": 12.0,
                "min": 1.0,
                "max": 50.0,
                "step": 0.5,
                "description": "Price drop percentage the strategy must survive",
            },
            {
                "name": "base_spacing",
                "label": "Base Spacing ($)",
                "type": "float",
                "default": 1.0,
                "min": 0.1,
                "max": 20.0,
                "step": 0.1,
                "description": "Base distance between grid levels",
            },
            {
                "name": "tp_mode",
                "label": "Take Profit Mode",
                "type": "select",
                "default": "LINEAR",
                "options": ["FIXED", "SQRT", "LINEAR"],
                "description": "How take-profit scales with position depth",
            },
            {
                "name": "sizing_mode",
                "label": "Sizing Mode",
                "type": "select",
                "default": "UNIFORM",
                "options": ["UNIFORM", "LINEAR_SIZING", "THRESHOLD_SIZING"],
                "description": "Position sizing strategy (UNIFORM safest for crashes)",
            },
            {
                "name": "enable_velocity_filter",
                "label": "Velocity Filter",
                "type": "bool",
                "default": True,
                "description": "Pause entries during fast price moves",
            },
            {
                "name": "force_min_volume_entry",
                "label": "Force Min Volume Entry",
                "type": "bool",
                "default": False,
                "description": "CRITICAL: must be false for production (crash survival)",
            },
        ],
    },
}


def get_all_strategies() -> list[dict]:
    """Return strategy list with metadata for the UI."""
    result = []
    for key, info in STRATEGY_REGISTRY.items():
        result.append(
            {
                "id": key,
                "name": info["name"],
                "description": info["description"],
                "cli_name": info["cli_name"],
                "parameters": info["parameters"],
            }
        )
    return result


def get_strategy(strategy_id: str) -> dict | None:
    """Get a specific strategy's metadata."""
    if strategy_id in STRATEGY_REGISTRY:
        info = STRATEGY_REGISTRY[strategy_id]
        return {
            "id": strategy_id,
            "name": info["name"],
            "description": info["description"],
            "cli_name": info["cli_name"],
            "parameters": info["parameters"],
        }
    return None
