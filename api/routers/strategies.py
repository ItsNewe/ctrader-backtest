"""Strategy listing endpoints."""

from fastapi import APIRouter

from api.services.strategy_registry import get_all_strategies, get_strategy

router = APIRouter(prefix="/api/strategies", tags=["strategies"])


@router.get("")
async def list_strategies():
    """Get all available strategies with their parameters."""
    return {"status": "success", "strategies": get_all_strategies()}


@router.get("/{strategy_id}")
async def get_strategy_detail(strategy_id: str):
    """Get a specific strategy's details."""
    strategy = get_strategy(strategy_id)
    if not strategy:
        return {"status": "error", "message": f"Strategy '{strategy_id}' not found"}
    return {"status": "success", "strategy": strategy}
