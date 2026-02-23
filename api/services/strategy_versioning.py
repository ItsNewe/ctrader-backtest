"""
Strategy versioning -- save/load parameter sets with names and notes.
"""

import json
import logging
import time
from pathlib import Path
from typing import Optional

from api.config import get_settings

logger = logging.getLogger(__name__)


def _versions_dir() -> Path:
    settings = get_settings()
    d = settings.results_dir / "strategy_versions"
    d.mkdir(parents=True, exist_ok=True)
    return d


def _versions_file(strategy_id: str) -> Path:
    return _versions_dir() / f"{strategy_id}_versions.json"


def _load_versions(strategy_id: str) -> list:
    f = _versions_file(strategy_id)
    if f.exists():
        return json.loads(f.read_text())
    return []


def _save_versions(strategy_id: str, versions: list):
    f = _versions_file(strategy_id)
    f.write_text(json.dumps(versions, indent=2))


def save_version(strategy_id: str, name: str, params: dict, notes: str = "", metrics: dict = None) -> dict:
    """Save a named parameter version."""
    versions = _load_versions(strategy_id)
    version = {
        "id": len(versions) + 1,
        "name": name,
        "strategy_id": strategy_id,
        "params": params,
        "notes": notes,
        "metrics": metrics or {},
        "created_at": time.time(),
    }
    versions.append(version)
    _save_versions(strategy_id, versions)
    return version


def list_versions(strategy_id: str) -> list:
    """List all saved versions for a strategy."""
    return _load_versions(strategy_id)


def get_version(strategy_id: str, version_id: int) -> Optional[dict]:
    """Get a specific version."""
    versions = _load_versions(strategy_id)
    for v in versions:
        if v["id"] == version_id:
            return v
    return None


def delete_version(strategy_id: str, version_id: int) -> bool:
    """Delete a version."""
    versions = _load_versions(strategy_id)
    new_versions = [v for v in versions if v["id"] != version_id]
    if len(new_versions) == len(versions):
        return False
    _save_versions(strategy_id, new_versions)
    return True
