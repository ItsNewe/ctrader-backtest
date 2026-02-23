"""Commission model endpoints."""

from fastapi import APIRouter
from api.services.commission_models import get_all_presets, get_preset

router = APIRouter(prefix="/api/commissions", tags=["commissions"])


@router.get("")
async def list_presets():
    """List all commission presets."""
    return {"status": "ok", "presets": get_all_presets()}


@router.get("/{preset_id}")
async def get_commission_preset(preset_id: str):
    """Get a specific commission preset."""
    preset = get_preset(preset_id)
    return {"status": "ok", "preset": preset}
