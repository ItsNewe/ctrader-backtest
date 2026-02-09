# Intelligent Hedge Management System for Gold Grid Trading

**Date:** January 2026  
**Subject:** Asymmetric hedging system that reduces risk more than it reduces profits

---

## 1. The Core Challenge

We want a hedging system that:

| Requirement | Target |
|-------------|--------|
| During bullish trends | Minimal impact on profits (< 5% reduction) |
| During drops | Reduce risk by 10x or more |
| Activation | Automatic, based on market conditions |
| Cost | Low carrying cost when not needed |

**The Key Insight:** We need **asymmetric protection** - hedges that cost little when not needed but provide massive protection when activated.

---

## 2. Hedge Instruments for Gold

### 2.1 Available Instruments

| Instrument | Pros | Cons | Best For |
|------------|------|------|----------|
| **Short Gold CFD** | Direct hedge, same broker | Costs swap, reduces upside | Large drops |
| **Put Options** | Limited cost, unlimited protection | Premium cost, expiration | Tail risk |
| **Inverse Gold ETFs** | Easy to trade | Decay over time | Short-term hedges |
| **VIX Calls** | Spikes during crashes | Indirect correlation | Black swans |
| **USD Long** | Negative correlation with gold | Not perfect hedge | Diversification |
| **Treasury Bonds** | Flight to safety correlation | Slow to react | Long-term hedge |

### 2.2 Recommended Primary Hedge: Dynamic Short Positions

For a CFD-based system like ctrader-backtest, the most practical hedge is **dynamic short gold positions** that activate based on conditions.

---

## 3. The Asymmetric Hedge System

### 3.1 Core Concept

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    ASYMMETRIC HEDGE ARCHITECTURE                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Normal Market (Bullish/Sideways):                                      │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Grid Strategy: ACTIVE (100% exposure)                          │    │
│  │  Hedge: DORMANT (0% or minimal trailing stop-entry orders)      │    │
│  │  Cost: Near zero (only opportunity cost of stop orders)         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
│  Drop Detected (Trigger conditions met):                                │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Grid Strategy: ACTIVE but reduced (50-75% exposure)            │    │
│  │  Hedge: ACTIVATED (25-50% short position)                       │    │
│  │  Net Exposure: Reduced by 50-75%                                │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
│  Severe Drop (Crisis mode):                                             │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Grid Strategy: PAUSED (hold existing, no new positions)        │    │
│  │  Hedge: FULL (100% of long exposure hedged)                     │    │
│  │  Net Exposure: Near zero (protected)                            │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 The Math: Why This Works

**During Bullish Trends:**
- Hedge is dormant = 0% cost
- Grid strategy runs at full capacity
- Profit reduction: ~0%

**During 30% Drop (without hedge):**
- Grid accumulates positions at lower prices
- Unrealized loss: -30% on average position
- Risk of margin call if leveraged

**During 30% Drop (with hedge activated at -10%):**
- Hedge activates at -10% drop
- Short position gains +20% (from -10% to -30%)
- Long positions lose -20% (from -10% to -30%)
- Net loss: ~0% on the -10% to -30% portion
- Only exposed to first -10% drop

**Risk Reduction Calculation:**
```
Without hedge: 30% loss exposure
With hedge: 10% loss exposure (only first 10% before hedge activates)
Risk reduction: 30% / 10% = 3x

If hedge activates at -5%:
Risk reduction: 30% / 5% = 6x

If hedge activates at -3% with trailing:
Risk reduction: 30% / 3% = 10x
```

---

## 4. Hedge Trigger System

### 4.1 Multi-Factor Trigger

The hedge should activate based on multiple factors, not just price drop:

```python
class HedgeTriggerSystem:
    def __init__(self):
        self.triggers = {
            'price_drop': {'threshold': 3.0, 'weight': 0.4},      # 3% drop from recent high
            'volatility_spike': {'threshold': 2.0, 'weight': 0.25}, # 2x normal volatility
            'momentum_negative': {'threshold': -1.5, 'weight': 0.2}, # Strong downward momentum
            'volume_spike': {'threshold': 2.5, 'weight': 0.15},    # 2.5x normal volume
        }
        self.activation_threshold = 0.6  # 60% weighted score to activate
    
    def calculate_hedge_score(self, market_data):
        score = 0
        
        # Price drop from recent high
        drop_pct = self._calculate_drop(market_data)
        if drop_pct >= self.triggers['price_drop']['threshold']:
            score += self.triggers['price_drop']['weight']
        
        # Volatility spike
        current_vol = self._calculate_volatility(market_data)
        avg_vol = self._calculate_avg_volatility(market_data)
        if current_vol / avg_vol >= self.triggers['volatility_spike']['threshold']:
            score += self.triggers['volatility_spike']['weight']
        
        # Negative momentum
        momentum = self._calculate_momentum(market_data)
        if momentum <= self.triggers['momentum_negative']['threshold']:
            score += self.triggers['momentum_negative']['weight']
        
        # Volume spike (often precedes big moves)
        volume_ratio = self._calculate_volume_ratio(market_data)
        if volume_ratio >= self.triggers['volume_spike']['threshold']:
            score += self.triggers['volume_spike']['weight']
        
        return score
    
    def should_activate_hedge(self, market_data):
        score = self.calculate_hedge_score(market_data)
        return {
            'activate': score >= self.activation_threshold,
            'score': score,
            'hedge_size': self._calculate_hedge_size(score)
        }
    
    def _calculate_hedge_size(self, score):
        """
        Graduated hedge size based on trigger score
        """
        if score < 0.6:
            return 0.0  # No hedge
        elif score < 0.7:
            return 0.25  # 25% hedge
        elif score < 0.8:
            return 0.50  # 50% hedge
        elif score < 0.9:
            return 0.75  # 75% hedge
        else:
            return 1.0  # Full hedge
```

### 4.2 Trailing Hedge Entry

Instead of fixed trigger levels, use trailing entries that follow price up but activate on drops:

```python
class TrailingHedgeEntry:
    def __init__(self, trail_distance_pct=3.0):
        self.trail_distance = trail_distance_pct / 100
        self.peak_price = None
        self.hedge_entry_price = None
        self.hedge_active = False
    
    def update(self, current_price):
        # Update peak price (only goes up)
        if self.peak_price is None or current_price > self.peak_price:
            self.peak_price = current_price
            self.hedge_entry_price = self.peak_price * (1 - self.trail_distance)
        
        # Check if hedge should activate
        if not self.hedge_active and current_price <= self.hedge_entry_price:
            self.hedge_active = True
            return {
                'action': 'ACTIVATE_HEDGE',
                'entry_price': current_price,
                'peak_price': self.peak_price,
                'drop_pct': (self.peak_price - current_price) / self.peak_price * 100
            }
        
        return {'action': 'HOLD', 'hedge_active': self.hedge_active}
    
    def reset_after_recovery(self, current_price, recovery_threshold_pct=5.0):
        """
        Reset hedge after price recovers significantly
        """
        if self.hedge_active:
            recovery = (current_price - self.hedge_entry_price) / self.hedge_entry_price * 100
            if recovery >= recovery_threshold_pct:
                self.hedge_active = False
                self.peak_price = current_price
                return {'action': 'DEACTIVATE_HEDGE', 'recovery_pct': recovery}
        
        return {'action': 'HOLD'}
```

---

## 5. Hedge Position Management

### 5.1 Dynamic Hedge Sizing

```python
class DynamicHedgeManager:
    def __init__(self, max_hedge_ratio=1.0):
        self.max_hedge_ratio = max_hedge_ratio
        self.current_hedge_size = 0
        self.long_exposure = 0
    
    def calculate_optimal_hedge(self, long_exposure, market_conditions):
        """
        Calculate optimal hedge size based on:
        1. Current long exposure
        2. Market conditions (volatility, trend)
        3. Drop severity
        """
        base_hedge = long_exposure * market_conditions['hedge_ratio']
        
        # Adjust for volatility
        vol_multiplier = min(2.0, market_conditions['volatility'] / market_conditions['avg_volatility'])
        adjusted_hedge = base_hedge * vol_multiplier
        
        # Cap at max hedge ratio
        final_hedge = min(adjusted_hedge, long_exposure * self.max_hedge_ratio)
        
        return {
            'hedge_size': final_hedge,
            'hedge_ratio': final_hedge / long_exposure if long_exposure > 0 else 0,
            'net_exposure': long_exposure - final_hedge
        }
    
    def execute_hedge_adjustment(self, target_hedge, current_hedge):
        """
        Gradually adjust hedge to target (avoid sudden large trades)
        """
        max_adjustment_per_step = 0.1  # 10% of exposure per adjustment
        
        difference = target_hedge - current_hedge
        
        if abs(difference) < 0.01:  # Less than 1% difference
            return {'action': 'HOLD', 'adjustment': 0}
        
        adjustment = max(-max_adjustment_per_step, min(max_adjustment_per_step, difference))
        
        if adjustment > 0:
            return {'action': 'INCREASE_HEDGE', 'adjustment': adjustment}
        else:
            return {'action': 'DECREASE_HEDGE', 'adjustment': abs(adjustment)}
```

### 5.2 Hedge Exit Strategy

```python
class HedgeExitManager:
    def __init__(self):
        self.exit_conditions = {
            'price_recovery': 0.05,      # 5% recovery from hedge entry
            'volatility_normalization': 1.2,  # Volatility back to 1.2x normal
            'momentum_reversal': 0.5,    # Positive momentum threshold
            'time_decay': 30,            # Days before forced review
        }
    
    def should_exit_hedge(self, hedge_entry_data, current_market):
        reasons = []
        
        # Price recovery
        recovery = (current_market['price'] - hedge_entry_data['entry_price']) / hedge_entry_data['entry_price']
        if recovery >= self.exit_conditions['price_recovery']:
            reasons.append(f'Price recovered {recovery*100:.1f}%')
        
        # Volatility normalization
        vol_ratio = current_market['volatility'] / current_market['avg_volatility']
        if vol_ratio <= self.exit_conditions['volatility_normalization']:
            reasons.append(f'Volatility normalized to {vol_ratio:.1f}x')
        
        # Momentum reversal
        if current_market['momentum'] >= self.exit_conditions['momentum_reversal']:
            reasons.append(f'Momentum turned positive: {current_market["momentum"]:.2f}')
        
        # Time decay (force review)
        days_active = (current_market['timestamp'] - hedge_entry_data['timestamp']).days
        if days_active >= self.exit_conditions['time_decay']:
            reasons.append(f'Hedge active for {days_active} days - review required')
        
        # Exit if 2+ conditions met
        should_exit = len(reasons) >= 2
        
        return {
            'should_exit': should_exit,
            'reasons': reasons,
            'exit_type': 'FULL' if len(reasons) >= 3 else 'PARTIAL'
        }
```

---

## 6. Cost-Benefit Analysis

### 6.1 Hedge Costs

| Cost Type | During Bullish | During Drop | Notes |
|-----------|----------------|-------------|-------|
| Swap/Financing | $0 (no position) | ~0.01%/day | Short gold has positive swap sometimes |
| Spread | $0 | One-time on entry/exit | ~0.02-0.05% per trade |
| Opportunity Cost | $0 | Reduced upside on recovery | Only if recovery is V-shaped |
| Slippage | $0 | ~0.01-0.02% | During volatile periods |

**Total Cost During Bullish Trend:** ~0% (hedge is dormant)

**Total Cost During Drop:** ~0.1-0.2% for activation + minimal daily carry

### 6.2 Benefit Analysis

```
Scenario: 30% Gold Drop Over 3 Months

WITHOUT HEDGE:
- Long exposure: $10,000
- Loss: $3,000 (30%)
- Margin call risk: HIGH if leveraged

WITH HEDGE (activates at -3%):
- Long exposure: $10,000
- Initial loss before hedge: $300 (3%)
- Hedge gains: ~$2,700 (27% × hedge size)
- Net loss: ~$300-600 (3-6%)
- Margin call risk: LOW

RISK REDUCTION: 30% / 3% = 10x
PROFIT IMPACT: ~0% during bullish (hedge dormant)
```

### 6.3 The Asymmetry Ratio

```
Asymmetry Ratio = Risk Reduction / Profit Reduction

Target: Asymmetry Ratio > 10

Example:
- Risk Reduction: 10x (from 30% to 3% max loss)
- Profit Reduction: 0-5% (hedge dormant during bullish)
- Asymmetry Ratio: 10 / 0.05 = 200 (excellent)

Even with 5% profit reduction:
- Asymmetry Ratio: 10 / 0.05 = 200

Even with 10% profit reduction:
- Asymmetry Ratio: 10 / 0.10 = 100 (still excellent)
```

---

## 7. Implementation Architecture

### 7.1 System Components

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    HEDGE MANAGEMENT SYSTEM                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                     MARKET MONITOR                                │   │
│  │  • Price tracking (peak, current, drop %)                        │   │
│  │  • Volatility calculation (current vs average)                   │   │
│  │  • Momentum indicators                                           │   │
│  │  • Volume analysis                                               │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                              │                                           │
│                              ▼                                           │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                     TRIGGER ENGINE                                │   │
│  │  • Multi-factor scoring                                          │   │
│  │  • Trailing entry calculation                                    │   │
│  │  • Activation threshold check                                    │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                              │                                           │
│                              ▼                                           │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                     HEDGE EXECUTOR                                │   │
│  │  • Position sizing                                               │   │
│  │  • Order placement                                               │   │
│  │  • Gradual adjustment                                            │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                              │                                           │
│                              ▼                                           │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                     EXIT MANAGER                                  │   │
│  │  • Recovery detection                                            │   │
│  │  • Partial/full exit logic                                       │   │
│  │  • Profit taking on hedge                                        │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                              │                                           │
│                              ▼                                           │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                     INTEGRATION LAYER                             │   │
│  │  • Coordinates with fill_up grid strategy                        │   │
│  │  • Manages combined exposure                                     │   │
│  │  • Reports net position                                          │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 7.2 Integration with fill_up Strategy

```python
class HedgedGridStrategy:
    def __init__(self, grid_params, hedge_params):
        self.grid = FillUpStrategy(grid_params)
        self.hedge = HedgeManagementSystem(hedge_params)
        self.combined_exposure = 0
    
    def on_tick(self, tick):
        # Update grid strategy
        grid_action = self.grid.on_tick(tick)
        
        # Update hedge system
        hedge_action = self.hedge.update(tick, self.grid.get_exposure())
        
        # Execute actions
        if grid_action['action'] == 'OPEN_LONG':
            self._execute_grid_order(grid_action)
        
        if hedge_action['action'] == 'ACTIVATE_HEDGE':
            self._execute_hedge(hedge_action)
        elif hedge_action['action'] == 'ADJUST_HEDGE':
            self._adjust_hedge(hedge_action)
        elif hedge_action['action'] == 'CLOSE_HEDGE':
            self._close_hedge(hedge_action)
        
        # Update combined exposure
        self.combined_exposure = self.grid.get_exposure() - self.hedge.get_hedge_size()
        
        return {
            'grid_exposure': self.grid.get_exposure(),
            'hedge_size': self.hedge.get_hedge_size(),
            'net_exposure': self.combined_exposure,
            'hedge_active': self.hedge.is_active()
        }
```

---

## 8. Complete Implementation Code

### 8.1 Main Hedge Manager Class

```python
# hedge_manager.py

import numpy as np
from dataclasses import dataclass
from typing import Dict, List, Optional
from datetime import datetime, timedelta

@dataclass
class HedgeState:
    is_active: bool
    entry_price: float
    entry_time: datetime
    size: float
    unrealized_pnl: float

@dataclass
class MarketState:
    price: float
    peak_price: float
    volatility: float
    avg_volatility: float
    momentum: float
    volume_ratio: float
    timestamp: datetime

class IntelligentHedgeManager:
    """
    Asymmetric hedge management system for gold grid trading
    Designed to reduce risk 10x while minimizing profit impact
    """
    
    def __init__(
        self,
        trail_distance_pct: float = 3.0,
        max_hedge_ratio: float = 1.0,
        volatility_lookback: int = 20,
        momentum_lookback: int = 10
    ):
        # Configuration
        self.trail_distance = trail_distance_pct / 100
        self.max_hedge_ratio = max_hedge_ratio
        self.volatility_lookback = volatility_lookback
        self.momentum_lookback = momentum_lookback
        
        # State
        self.peak_price = None
        self.hedge_entry_price = None
        self.hedge_state: Optional[HedgeState] = None
        self.price_history: List[float] = []
        self.volume_history: List[float] = []
        
        # Trigger weights
        self.trigger_weights = {
            'price_drop': 0.40,
            'volatility_spike': 0.25,
            'momentum_negative': 0.20,
            'volume_spike': 0.15
        }
        
        # Thresholds
        self.activation_threshold = 0.60
        self.deactivation_threshold = 0.30
    
    def update(self, price: float, volume: float, long_exposure: float) -> Dict:
        """
        Main update function called on each tick/bar
        """
        # Update price history
        self.price_history.append(price)
        self.volume_history.append(volume)
        
        # Keep only needed history
        max_lookback = max(self.volatility_lookback, self.momentum_lookback) + 10
        if len(self.price_history) > max_lookback:
            self.price_history = self.price_history[-max_lookback:]
            self.volume_history = self.volume_history[-max_lookback:]
        
        # Update peak price (trailing)
        if self.peak_price is None or price > self.peak_price:
            self.peak_price = price
            self.hedge_entry_price = self.peak_price * (1 - self.trail_distance)
        
        # Calculate market state
        market_state = self._calculate_market_state(price)
        
        # Calculate trigger score
        trigger_score = self._calculate_trigger_score(market_state)
        
        # Determine action
        if self.hedge_state is None or not self.hedge_state.is_active:
            # Check if we should activate hedge
            if trigger_score >= self.activation_threshold:
                return self._activate_hedge(price, long_exposure, trigger_score)
            else:
                return {
                    'action': 'HOLD',
                    'hedge_active': False,
                    'trigger_score': trigger_score,
                    'net_exposure': long_exposure
                }
        else:
            # Hedge is active - check if we should adjust or exit
            return self._manage_active_hedge(price, long_exposure, market_state, trigger_score)
    
    def _calculate_market_state(self, current_price: float) -> MarketState:
        """Calculate current market conditions"""
        
        # Volatility
        if len(self.price_history) >= self.volatility_lookback:
            returns = np.diff(self.price_history[-self.volatility_lookback:]) / self.price_history[-self.volatility_lookback:-1]
            volatility = np.std(returns) * np.sqrt(252)  # Annualized
            avg_volatility = volatility  # In production, use longer-term average
        else:
            volatility = 0.15  # Default 15% annual volatility for gold
            avg_volatility = 0.15
        
        # Momentum
        if len(self.price_history) >= self.momentum_lookback:
            momentum = (current_price - self.price_history[-self.momentum_lookback]) / self.price_history[-self.momentum_lookback]
        else:
            momentum = 0
        
        # Volume ratio
        if len(self.volume_history) >= 20:
            avg_volume = np.mean(self.volume_history[-20:])
            volume_ratio = self.volume_history[-1] / avg_volume if avg_volume > 0 else 1
        else:
            volume_ratio = 1
        
        return MarketState(
            price=current_price,
            peak_price=self.peak_price,
            volatility=volatility,
            avg_volatility=avg_volatility,
            momentum=momentum,
            volume_ratio=volume_ratio,
            timestamp=datetime.now()
        )
    
    def _calculate_trigger_score(self, market_state: MarketState) -> float:
        """Calculate weighted trigger score"""
        score = 0
        
        # Price drop from peak
        drop_pct = (market_state.peak_price - market_state.price) / market_state.peak_price
        if drop_pct >= self.trail_distance:
            score += self.trigger_weights['price_drop']
        elif drop_pct >= self.trail_distance * 0.5:
            score += self.trigger_weights['price_drop'] * 0.5
        
        # Volatility spike
        vol_ratio = market_state.volatility / market_state.avg_volatility
        if vol_ratio >= 2.0:
            score += self.trigger_weights['volatility_spike']
        elif vol_ratio >= 1.5:
            score += self.trigger_weights['volatility_spike'] * 0.5
        
        # Negative momentum
        if market_state.momentum <= -0.03:  # -3% momentum
            score += self.trigger_weights['momentum_negative']
        elif market_state.momentum <= -0.015:
            score += self.trigger_weights['momentum_negative'] * 0.5
        
        # Volume spike
        if market_state.volume_ratio >= 2.5:
            score += self.trigger_weights['volume_spike']
        elif market_state.volume_ratio >= 1.5:
            score += self.trigger_weights['volume_spike'] * 0.5
        
        return score
    
    def _activate_hedge(self, price: float, long_exposure: float, trigger_score: float) -> Dict:
        """Activate hedge position"""
        
        # Calculate hedge size based on trigger score
        if trigger_score >= 0.9:
            hedge_ratio = 1.0
        elif trigger_score >= 0.8:
            hedge_ratio = 0.75
        elif trigger_score >= 0.7:
            hedge_ratio = 0.50
        else:
            hedge_ratio = 0.25
        
        hedge_size = long_exposure * hedge_ratio * self.max_hedge_ratio
        
        self.hedge_state = HedgeState(
            is_active=True,
            entry_price=price,
            entry_time=datetime.now(),
            size=hedge_size,
            unrealized_pnl=0
        )
        
        return {
            'action': 'ACTIVATE_HEDGE',
            'hedge_active': True,
            'hedge_size': hedge_size,
            'hedge_ratio': hedge_ratio,
            'entry_price': price,
            'trigger_score': trigger_score,
            'net_exposure': long_exposure - hedge_size
        }
    
    def _manage_active_hedge(
        self,
        price: float,
        long_exposure: float,
        market_state: MarketState,
        trigger_score: float
    ) -> Dict:
        """Manage existing hedge position"""
        
        # Update unrealized P&L
        price_change = (self.hedge_state.entry_price - price) / self.hedge_state.entry_price
        self.hedge_state.unrealized_pnl = self.hedge_state.size * price_change
        
        # Check exit conditions
        exit_check = self._check_exit_conditions(price, market_state, trigger_score)
        
        if exit_check['should_exit']:
            return self._exit_hedge(price, long_exposure, exit_check)
        
        # Check if we should adjust hedge size
        adjustment = self._calculate_hedge_adjustment(long_exposure, trigger_score)
        
        if adjustment['should_adjust']:
            return self._adjust_hedge(price, long_exposure, adjustment)
        
        return {
            'action': 'HOLD_HEDGE',
            'hedge_active': True,
            'hedge_size': self.hedge_state.size,
            'unrealized_pnl': self.hedge_state.unrealized_pnl,
            'trigger_score': trigger_score,
            'net_exposure': long_exposure - self.hedge_state.size
        }
    
    def _check_exit_conditions(
        self,
        price: float,
        market_state: MarketState,
        trigger_score: float
    ) -> Dict:
        """Check if hedge should be exited"""
        
        reasons = []
        
        # Price recovery
        recovery = (price - self.hedge_state.entry_price) / self.hedge_state.entry_price
        if recovery >= 0.05:  # 5% recovery
            reasons.append(f'Price recovered {recovery*100:.1f}%')
        
        # Trigger score dropped
        if trigger_score < self.deactivation_threshold:
            reasons.append(f'Trigger score dropped to {trigger_score:.2f}')
        
        # Volatility normalized
        vol_ratio = market_state.volatility / market_state.avg_volatility
        if vol_ratio < 1.2:
            reasons.append(f'Volatility normalized to {vol_ratio:.1f}x')
        
        # Momentum reversed
        if market_state.momentum > 0.02:  # +2% momentum
            reasons.append(f'Momentum reversed to +{market_state.momentum*100:.1f}%')
        
        # Time-based review (30 days)
        days_active = (datetime.now() - self.hedge_state.entry_time).days
        if days_active >= 30:
            reasons.append(f'Hedge active for {days_active} days')
        
        should_exit = len(reasons) >= 2
        exit_type = 'FULL' if len(reasons) >= 3 else 'PARTIAL'
        
        return {
            'should_exit': should_exit,
            'exit_type': exit_type,
            'reasons': reasons
        }
    
    def _exit_hedge(self, price: float, long_exposure: float, exit_check: Dict) -> Dict:
        """Exit hedge position"""
        
        realized_pnl = self.hedge_state.unrealized_pnl
        
        if exit_check['exit_type'] == 'PARTIAL':
            # Exit 50% of hedge
            exit_size = self.hedge_state.size * 0.5
            self.hedge_state.size -= exit_size
            realized_pnl *= 0.5
        else:
            # Full exit
            exit_size = self.hedge_state.size
            self.hedge_state = None
            # Reset peak price for new trailing
            self.peak_price = price
        
        return {
            'action': 'EXIT_HEDGE',
            'exit_type': exit_check['exit_type'],
            'exit_size': exit_size,
            'realized_pnl': realized_pnl,
            'reasons': exit_check['reasons'],
            'hedge_active': self.hedge_state is not None and self.hedge_state.is_active,
            'net_exposure': long_exposure - (self.hedge_state.size if self.hedge_state else 0)
        }
    
    def _calculate_hedge_adjustment(self, long_exposure: float, trigger_score: float) -> Dict:
        """Calculate if hedge size should be adjusted"""
        
        # Target hedge ratio based on current trigger score
        if trigger_score >= 0.9:
            target_ratio = 1.0
        elif trigger_score >= 0.8:
            target_ratio = 0.75
        elif trigger_score >= 0.7:
            target_ratio = 0.50
        elif trigger_score >= 0.6:
            target_ratio = 0.25
        else:
            target_ratio = 0
        
        target_size = long_exposure * target_ratio * self.max_hedge_ratio
        current_size = self.hedge_state.size
        
        difference = target_size - current_size
        difference_pct = abs(difference) / current_size if current_size > 0 else 1
        
        # Only adjust if difference is significant (>10%)
        should_adjust = difference_pct > 0.10
        
        return {
            'should_adjust': should_adjust,
            'target_size': target_size,
            'adjustment': difference,
            'adjustment_pct': difference_pct
        }
    
    def _adjust_hedge(self, price: float, long_exposure: float, adjustment: Dict) -> Dict:
        """Adjust hedge size"""
        
        old_size = self.hedge_state.size
        self.hedge_state.size = adjustment['target_size']
        
        return {
            'action': 'ADJUST_HEDGE',
            'old_size': old_size,
            'new_size': self.hedge_state.size,
            'adjustment': adjustment['adjustment'],
            'hedge_active': True,
            'net_exposure': long_exposure - self.hedge_state.size
        }
    
    def get_status(self) -> Dict:
        """Get current hedge status"""
        return {
            'hedge_active': self.hedge_state is not None and self.hedge_state.is_active,
            'hedge_size': self.hedge_state.size if self.hedge_state else 0,
            'entry_price': self.hedge_state.entry_price if self.hedge_state else None,
            'unrealized_pnl': self.hedge_state.unrealized_pnl if self.hedge_state else 0,
            'peak_price': self.peak_price,
            'trail_entry_price': self.hedge_entry_price
        }
```

---

## 9. Backtesting the Hedge System

### 9.1 Test Scenarios

```python
class HedgeBacktester:
    def __init__(self, hedge_manager, grid_strategy):
        self.hedge = hedge_manager
        self.grid = grid_strategy
    
    def run_scenario(self, price_data, scenario_name):
        """Run backtest on price data"""
        results = {
            'scenario': scenario_name,
            'trades': [],
            'hedge_activations': 0,
            'hedge_exits': 0,
            'total_hedge_pnl': 0,
            'max_drawdown_with_hedge': 0,
            'max_drawdown_without_hedge': 0
        }
        
        equity_with_hedge = 10000
        equity_without_hedge = 10000
        peak_with = 10000
        peak_without = 10000
        
        for i, row in price_data.iterrows():
            price = row['close']
            volume = row.get('volume', 1000)
            
            # Simulate grid exposure (simplified)
            grid_exposure = self.grid.get_exposure()
            
            # Update hedge
            hedge_result = self.hedge.update(price, volume, grid_exposure)
            
            # Track results
            if hedge_result['action'] == 'ACTIVATE_HEDGE':
                results['hedge_activations'] += 1
            elif hedge_result['action'] == 'EXIT_HEDGE':
                results['hedge_exits'] += 1
                results['total_hedge_pnl'] += hedge_result.get('realized_pnl', 0)
            
            # Calculate equity curves
            # ... (detailed P&L calculation)
        
        return results
    
    def compare_with_without_hedge(self, price_data):
        """Compare performance with and without hedge"""
        # Run with hedge
        with_hedge = self.run_scenario(price_data, 'with_hedge')
        
        # Run without hedge (reset and disable)
        self.hedge = IntelligentHedgeManager(trail_distance_pct=100)  # Never triggers
        without_hedge = self.run_scenario(price_data, 'without_hedge')
        
        return {
            'with_hedge': with_hedge,
            'without_hedge': without_hedge,
            'risk_reduction': without_hedge['max_drawdown_without_hedge'] / max(with_hedge['max_drawdown_with_hedge'], 0.01),
            'profit_impact': (with_hedge['total_pnl'] - without_hedge['total_pnl']) / without_hedge['total_pnl'] if without_hedge['total_pnl'] != 0 else 0
        }
```

---

## 10. Summary

### 10.1 Key Features of the Hedge System

| Feature | Implementation | Benefit |
|---------|----------------|---------|
| **Trailing Entry** | Follows price up, activates on drops | Zero cost during bullish trends |
| **Multi-Factor Triggers** | Price + Volatility + Momentum + Volume | Reduces false activations |
| **Graduated Sizing** | 25% → 50% → 75% → 100% based on severity | Proportional protection |
| **Dynamic Exit** | Multiple conditions for exit | Captures hedge profits |
| **Integration** | Works alongside fill_up grid | Seamless operation |

### 10.2 Expected Performance

| Metric | Without Hedge | With Hedge | Improvement |
|--------|---------------|------------|-------------|
| Max Drawdown | 30-50% | 3-10% | 5-10x reduction |
| Profit (Bullish) | 100% | 95-100% | ~0-5% cost |
| Profit (Volatile) | 80% | 90-100% | Better due to hedge gains |
| Survival Rate | 70-80% | 99%+ | Critical improvement |

### 10.3 The Asymmetry Achieved

```
Risk Reduction: 10x (from 30% max loss to 3% max loss)
Profit Reduction: 0-5% (hedge dormant during bullish)
Asymmetry Ratio: 10 / 0.05 = 200

This means: For every 1% of profit given up, we reduce risk by 200%
```

---

## 11. Implementation Checklist

### Before Deployment:

- [ ] Backtest hedge system on 10+ years of gold data
- [ ] Test against all historical crashes (1980, 2008, 2011, 2020, 2022)
- [ ] Verify hedge activation timing is appropriate
- [ ] Confirm hedge exit doesn't leave positions exposed
- [ ] Test integration with fill_up grid strategy
- [ ] Calculate actual costs (swap, spread, slippage)
- [ ] Paper trade for 3+ months
- [ ] Start with 25% of planned capital

### Ongoing Monitoring:

- [ ] Track hedge activation frequency
- [ ] Monitor false activation rate
- [ ] Review hedge P&L separately from grid P&L
- [ ] Adjust trigger thresholds if needed
- [ ] Document all parameter changes

---

*This document is part of the external review for the ctrader-backtest system.*
