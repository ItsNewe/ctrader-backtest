"""
Strategy metadata registry.
Maps strategy names to their parameters, defaults, and CLI identifiers.
Must match the C++ Config structs exactly.

Each parameter has:
- name: Python/CLI key name (matches C++ Config field)
- label: Display name for UI
- type: "float" | "int" | "bool" | "select"
- default: Default value
- min/max/step: For numeric types
- options: For select types
- description: Help text
- advanced: If True, hidden behind toggle in UI (optional, default False)
"""

import logging
from typing import Optional

logger = logging.getLogger(__name__)


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
                    "ADAPTIVE_LOOKBACK",
                    "DOUBLE_ADAPTIVE",
                    "TREND_ADAPTIVE",
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
                "description": "Scale factor for antifragile sizing (10% more per 5% DD)",
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
                "description": "Always enter with min volume (safer=false for crashes)",
            },
            # Advanced parameters
            {
                "name": "typical_vol_pct",
                "label": "Typical Volatility %",
                "type": "float",
                "default": 0.55,
                "min": 0.05,
                "max": 5.0,
                "step": 0.05,
                "description": "Expected typical volatility percentage for adaptive spacing",
                "advanced": True,
            },
            {
                "name": "min_spacing_mult",
                "label": "Min Spacing Multiplier",
                "type": "float",
                "default": 0.5,
                "min": 0.1,
                "max": 5.0,
                "step": 0.1,
                "description": "Minimum adaptive spacing multiplier",
                "advanced": True,
            },
        ],
    },
    "CombinedJu": {
        "name": "Combined Ju",
        "description": "Highest returns: Rubber Band TP + Velocity Filter + Barbell Sizing",
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
            {
                "name": "pct_spacing",
                "label": "Percentage Spacing",
                "type": "bool",
                "default": False,
                "description": "Use percentage-based spacing (required for XAGUSD)",
            },
            # Advanced: Rubber Band TP tuning
            {
                "name": "tp_sqrt_scale",
                "label": "TP Sqrt Scale",
                "type": "float",
                "default": 0.5,
                "min": 0.1,
                "max": 3.0,
                "step": 0.1,
                "description": "Scale factor for SQRT take-profit mode",
                "advanced": True,
            },
            {
                "name": "tp_linear_scale",
                "label": "TP Linear Scale",
                "type": "float",
                "default": 0.3,
                "min": 0.05,
                "max": 2.0,
                "step": 0.05,
                "description": "Scale factor for LINEAR take-profit mode",
                "advanced": True,
            },
            {
                "name": "tp_min",
                "label": "Minimum TP ($)",
                "type": "float",
                "default": 1.50,
                "min": 0.1,
                "max": 20.0,
                "step": 0.1,
                "description": "Minimum take-profit distance",
                "advanced": True,
            },
            # Advanced: Velocity filter tuning
            {
                "name": "velocity_window",
                "label": "Velocity Window (ticks)",
                "type": "int",
                "default": 10,
                "min": 3,
                "max": 100,
                "step": 1,
                "description": "Number of ticks for velocity calculation",
                "advanced": True,
            },
            {
                "name": "velocity_threshold_pct",
                "label": "Velocity Threshold %",
                "type": "float",
                "default": 0.01,
                "min": 0.001,
                "max": 0.1,
                "step": 0.001,
                "description": "Velocity threshold as percentage of price",
                "advanced": True,
            },
            {
                "name": "typical_vol_pct",
                "label": "Typical Volatility %",
                "type": "float",
                "default": 0.55,
                "min": 0.05,
                "max": 5.0,
                "step": 0.05,
                "description": "Expected typical volatility for spacing calibration",
                "advanced": True,
            },
            # Advanced: Barbell sizing tuning
            {
                "name": "sizing_linear_scale",
                "label": "Sizing Linear Scale",
                "type": "float",
                "default": 0.5,
                "min": 0.1,
                "max": 3.0,
                "step": 0.1,
                "description": "Scale factor for LINEAR_SIZING mode",
                "advanced": True,
            },
            {
                "name": "sizing_threshold_pos",
                "label": "Sizing Threshold Position",
                "type": "int",
                "default": 5,
                "min": 1,
                "max": 50,
                "step": 1,
                "description": "Position count threshold for THRESHOLD_SIZING mode",
                "advanced": True,
            },
            {
                "name": "sizing_threshold_mult",
                "label": "Sizing Threshold Multiplier",
                "type": "float",
                "default": 2.0,
                "min": 1.0,
                "max": 10.0,
                "step": 0.5,
                "description": "Lot multiplier after threshold in THRESHOLD_SIZING mode",
                "advanced": True,
            },
        ],
    },
    "CombinedOUFU": {
        "name": "Combined OU+FU",
        "description": "Three sub-strategies: OU Down grid, OU Up continuous, Fill Up with TP",
        "cli_name": "oufu",
        "parameters": [
            {
                "name": "base_survive",
                "label": "Base Survive %",
                "type": "float",
                "default": 5.0,
                "min": 1.0,
                "max": 50.0,
                "step": 0.5,
                "description": "Base survive percentage (multiplied per sub-strategy)",
            },
            {
                "name": "mult_ou_down",
                "label": "OU Down Multiplier",
                "type": "float",
                "default": 1.0,
                "min": 0.0,
                "max": 10.0,
                "step": 0.1,
                "description": "Survive multiplier for OU Down grid (0=disable)",
            },
            {
                "name": "mult_ou_up",
                "label": "OU Up Multiplier",
                "type": "float",
                "default": 2.0,
                "min": 0.0,
                "max": 10.0,
                "step": 0.1,
                "description": "Survive multiplier for OU Up continuous (0=disable)",
            },
            {
                "name": "mult_fu",
                "label": "FU Multiplier",
                "type": "float",
                "default": 0.5,
                "min": 0.0,
                "max": 10.0,
                "step": 0.1,
                "description": "Survive multiplier for Fill Up (0=disable)",
            },
            {
                "name": "fu_spacing",
                "label": "FU Spacing ($)",
                "type": "float",
                "default": 1.0,
                "min": 0.1,
                "max": 20.0,
                "step": 0.1,
                "description": "Grid spacing for the Fill Up sub-strategy",
            },
            {
                "name": "ou_sizing",
                "label": "OU Sizing Mode",
                "type": "select",
                "default": "0",
                "options": ["0", "1"],
                "description": "OU Down sizing: 0=constant, 1=incremental",
            },
            {
                "name": "ou_closing_mode",
                "label": "OU Closing Mode",
                "type": "select",
                "default": "0",
                "options": ["0", "1"],
                "description": "0=close profitable on reversal, 1=close all if none unprofitable",
            },
            # Advanced parameters
            {
                "name": "max_number_of_trades",
                "label": "Max OU Up Trades",
                "type": "int",
                "default": 200,
                "min": 10,
                "max": 1000,
                "step": 10,
                "description": "Max simultaneous OU Up positions before closing smallest",
                "advanced": True,
            },
            {
                "name": "fu_size",
                "label": "FU Size Multiplier",
                "type": "float",
                "default": 1.0,
                "min": 0.1,
                "max": 10.0,
                "step": 0.1,
                "description": "Size multiplier for Fill Up entries",
                "advanced": True,
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


def validate_strategy_params(strategy_id: str, params: dict) -> dict:
    """Validate and clean strategy parameters against the registry.

    - Fills in defaults for missing parameters
    - Validates types and bounds
    - Warns on unknown parameters (but passes them through for flexibility)
    - Raises ValueError for out-of-bounds values

    Returns cleaned params dict.
    """
    strategy_info = STRATEGY_REGISTRY.get(strategy_id)
    if not strategy_info:
        raise ValueError(f"Unknown strategy: {strategy_id}")

    param_defs = {p["name"]: p for p in strategy_info["parameters"]}
    cleaned = {}

    # Fill in defaults for missing params
    for name, pdef in param_defs.items():
        if name not in params:
            cleaned[name] = pdef["default"]
        else:
            value = params[name]

            # Type validation
            if pdef["type"] == "float":
                try:
                    value = float(value)
                except (TypeError, ValueError):
                    raise ValueError(
                        f"Parameter '{name}' must be a number, got: {value}"
                    )
                # Bounds check
                if "min" in pdef and value < pdef["min"]:
                    raise ValueError(
                        f"Parameter '{name}' = {value} is below minimum {pdef['min']}"
                    )
                if "max" in pdef and value > pdef["max"]:
                    raise ValueError(
                        f"Parameter '{name}' = {value} exceeds maximum {pdef['max']}"
                    )

            elif pdef["type"] == "int":
                try:
                    value = int(value)
                except (TypeError, ValueError):
                    raise ValueError(
                        f"Parameter '{name}' must be an integer, got: {value}"
                    )
                if "min" in pdef and value < pdef["min"]:
                    raise ValueError(
                        f"Parameter '{name}' = {value} is below minimum {pdef['min']}"
                    )
                if "max" in pdef and value > pdef["max"]:
                    raise ValueError(
                        f"Parameter '{name}' = {value} exceeds maximum {pdef['max']}"
                    )

            elif pdef["type"] == "bool":
                if isinstance(value, str):
                    value = value.lower() in ("true", "1", "yes")
                else:
                    value = bool(value)

            elif pdef["type"] == "select":
                if "options" in pdef and str(value) not in pdef["options"]:
                    raise ValueError(
                        f"Parameter '{name}' = '{value}' not in options: {pdef['options']}"
                    )

            cleaned[name] = value

    # Pass through unknown params with a warning (for forward compatibility)
    for name, value in params.items():
        if name not in param_defs:
            logger.warning(f"Unknown parameter '{name}' for strategy {strategy_id}")
            cleaned[name] = value

    return cleaned
