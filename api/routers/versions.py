"""Strategy versioning endpoints."""

from fastapi import APIRouter
from api.services.strategy_versioning import save_version, list_versions, get_version, delete_version

router = APIRouter(prefix="/api/versions", tags=["versions"])


@router.get("/{strategy_id}")
async def api_list_versions(strategy_id: str):
    """List all saved parameter versions for a strategy."""
    versions = list_versions(strategy_id)
    return {"status": "ok", "versions": versions}


@router.post("/{strategy_id}")
async def api_save_version(strategy_id: str, body: dict):
    """Save a parameter version."""
    version = save_version(
        strategy_id=strategy_id,
        name=body.get("name", "Untitled"),
        params=body.get("params", {}),
        notes=body.get("notes", ""),
        metrics=body.get("metrics"),
    )
    return {"status": "ok", "version": version}


@router.get("/{strategy_id}/{version_id}")
async def api_get_version(strategy_id: str, version_id: int):
    """Get a specific version."""
    version = get_version(strategy_id, version_id)
    if not version:
        return {"status": "error", "message": "Version not found"}
    return {"status": "ok", "version": version}


@router.delete("/{strategy_id}/{version_id}")
async def api_delete_version(strategy_id: str, version_id: int):
    """Delete a version."""
    success = delete_version(strategy_id, version_id)
    if success:
        return {"status": "ok", "message": "Version deleted"}
    return {"status": "error", "message": "Version not found"}
