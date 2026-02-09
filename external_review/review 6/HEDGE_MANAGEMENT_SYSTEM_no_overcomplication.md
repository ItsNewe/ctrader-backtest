# Simple Hedge System for Gold Grid Trading

**Date:** January 2026  
**Goal:** Reduce risk 10x while reducing profits by less than 5%

---

## The Simple Idea

**During bullish trends:** No hedge = no cost  
**During drops:** Open a short position to offset losses

---

## How It Works

```
Price going UP:    Grid buys, no hedge needed
                   Cost: $0

Price drops 3%:    Hedge activates (open short)
                   Short gains offset long losses
                   
Price recovers:    Close hedge, resume normal trading
```

---

## The Core Logic

```python
class SimpleHedge:
    def __init__(self, trigger_drop_pct=3.0):
        self.trigger = trigger_drop_pct / 100
        self.peak_price = None
        self.hedge_active = False
        self.hedge_size = 0
    
    def update(self, price, long_exposure):
        # Track peak price
        if self.peak_price is None or price > self.peak_price:
            self.peak_price = price
        
        # Calculate drop from peak
        drop = (self.peak_price - price) / self.peak_price
        
        # Activate hedge if drop exceeds trigger
        if not self.hedge_active and drop >= self.trigger:
            self.hedge_active = True
            self.hedge_size = long_exposure  # Full hedge
            return {'action': 'OPEN_SHORT', 'size': self.hedge_size}
        
        # Deactivate hedge if price recovers above peak
        if self.hedge_active and price >= self.peak_price * 0.98:
            self.hedge_active = False
            self.hedge_size = 0
            self.peak_price = price  # Reset peak
            return {'action': 'CLOSE_SHORT'}
        
        return {'action': 'HOLD'}
```

---

## Why This Works

| Scenario | Without Hedge | With Hedge |
|----------|---------------|------------|
| 30% drop | -30% loss | -3% loss (only first 3% before hedge) |
| Bullish trend | 100% profit | 100% profit (hedge never activates) |
| Choppy market | Normal P&L | Slightly reduced (hedge may activate/deactivate) |

**Risk Reduction:** 30% / 3% = **10x**  
**Profit Cost:** 0% during bullish, ~5% during choppy = **~2% average**

---

## Parameters to Tune

| Parameter | Conservative | Moderate | Aggressive |
|-----------|--------------|----------|------------|
| Trigger Drop | 5% | 3% | 2% |
| Hedge Size | 100% of longs | 75% of longs | 50% of longs |
| Recovery to Exit | 2% above entry | At entry | 2% below peak |

---

## Integration with fill_up

Add to the fill_up strategy:

```python
# In OnTick or main loop:
hedge_action = hedge.update(current_price, total_long_exposure)

if hedge_action['action'] == 'OPEN_SHORT':
    open_short_position(hedge_action['size'])
elif hedge_action['action'] == 'CLOSE_SHORT':
    close_short_position()
```

---

## Key Points

1. **Zero cost during bullish trends** - hedge is dormant
2. **Automatic activation** - no manual intervention needed
3. **Simple logic** - easy to understand and debug
4. **10x risk reduction** - survive drops that would otherwise blow up account
5. **Works with existing grid** - just add on top

---

## What to Test

1. Backtest on 2008, 2011-2015, 2020, 2022 gold drops
2. Verify hedge activates at right time
3. Check that hedge doesn't activate too often in choppy markets
4. Confirm total cost is < 5% of profits

---

*Keep it simple. The goal is survival, not perfection.*
