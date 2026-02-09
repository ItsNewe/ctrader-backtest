# Gold Trading Strategy Framework: Controllables vs Uncontrollables

**Date:** January 2026  
**Subject:** Framework for trading gold with focus on what we can and cannot control

---

## 1. The Core Insight

You've identified a fundamental truth about gold:

| What We Know | What We Don't Know |
|--------------|-------------------|
| Gold tends to appreciate over long timeframes | When drops will occur |
| Gold fluctuates around its trend | How deep drops will be |
| Drops of 20-40% have occurred historically | How long drops will last |
| Gold has intrinsic value (won't go to zero) | Exact timing of recoveries |

This asymmetry creates an opportunity: **We can design strategies that exploit what we know while protecting against what we don't know.**

---

## 2. Controllables vs Uncontrollables Framework

### 2.1 What We CAN Control

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         CONTROLLABLES                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐       │
│  │ Position Sizing  │  │ Entry Rules      │  │ Exit Rules       │       │
│  │                  │  │                  │  │                  │       │
│  │ • Max % of       │  │ • When to add    │  │ • Take profit    │       │
│  │   account        │  │   positions      │  │   levels         │       │
│  │ • Scaling rules  │  │ • Grid spacing   │  │ • Stop loss      │       │
│  │ • Margin limits  │  │ • DCA triggers   │  │   conditions     │       │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘       │
│                                                                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐       │
│  │ Risk Limits      │  │ Time Horizon     │  │ Hedging          │       │
│  │                  │  │                  │  │                  │       │
│  │ • Max drawdown   │  │ • How long we    │  │ • Options        │       │
│  │   tolerance      │  │   can wait       │  │ • Inverse ETFs   │       │
│  │ • Account        │  │ • Funding        │  │ • Correlated     │       │
│  │   survival       │  │   availability   │  │   assets         │       │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘       │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 What We CANNOT Control

```
┌─────────────────────────────────────────────────────────────────────────┐
│                       UNCONTROLLABLES                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐       │
│  │ Market Direction │  │ Volatility       │  │ Black Swans      │       │
│  │                  │  │                  │  │                  │       │
│  │ • Short-term     │  │ • Spike timing   │  │ • Flash crashes  │       │
│  │   movements      │  │ • Duration       │  │ • Liquidity      │       │
│  │ • Trend changes  │  │ • Magnitude      │  │   events         │       │
│  │ • Reversals      │  │                  │  │ • Broker issues  │       │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘       │
│                                                                          │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐       │
│  │ Macro Events     │  │ Spreads/Costs    │  │ Execution        │       │
│  │                  │  │                  │  │                  │       │
│  │ • Fed decisions  │  │ • Broker changes │  │ • Slippage       │       │
│  │ • Geopolitics    │  │ • Swap rates     │  │ • Requotes       │       │
│  │ • Inflation      │  │ • Commission     │  │ • Gaps           │       │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘       │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Strategy Design Principles for Gold

### 3.1 The Core Thesis

**Gold's Long-Term Behavior:**
- Tends to appreciate over decades (inflation hedge, store of value)
- Has never gone to zero in 5000+ years of history
- Recovers from all drawdowns eventually

**Implication:** If we can survive any drawdown, we will eventually profit.

### 3.2 The Survival Equation

```
Account Survival = f(Position Size, Drawdown Tolerance, Time Horizon, Funding)
```

**Key Variables:**

| Variable | Definition | Control Level |
|----------|------------|---------------|
| Position Size | How much exposure per unit of capital | Full Control |
| Drawdown Tolerance | Max % loss before margin call | Partial Control (leverage dependent) |
| Time Horizon | How long we can hold losing positions | Full Control (if funded) |
| Funding | Additional capital to add during drawdowns | Partial Control |

### 3.3 The fill_up Strategy in This Context

Looking at the fill_up strategy parameters:

```cpp
input double survive = 2.5;  // Survival threshold %
input double size = 1;       // Position size multiplier
input double spacing = 1;    // Grid spacing
```

**How these relate to controllables:**

| Parameter | Controls | Risk Impact |
|-----------|----------|-------------|
| `survive` | How much drop the strategy can withstand | Higher = safer but lower returns |
| `size` | Position sizing per grid level | Lower = safer but slower accumulation |
| `spacing` | Distance between grid entries | Wider = fewer positions, less margin use |

---

## 4. Designing for Unknown Drops

### 4.1 Historical Gold Drawdowns

| Period | Drop | Duration | Recovery Time |
|--------|------|----------|---------------|
| 1980-1982 | -65% | 2 years | 28 years |
| 2011-2015 | -45% | 4 years | 5 years |
| 2020 (COVID) | -15% | 1 month | 2 months |
| 2022 | -22% | 8 months | 1 year |

**Key Insight:** The worst drop was 65%, but it took 28 years to recover. Most drops are 20-40% and recover within 1-5 years.

### 4.2 Strategy Tiers Based on Drop Scenarios

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    TIERED SURVIVAL STRATEGY                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Tier 1: Normal Fluctuations (0-10% drop)                               │
│  ├── Full grid trading active                                           │
│  ├── Normal position sizes                                              │
│  └── Take profits on bounces                                            │
│                                                                          │
│  Tier 2: Moderate Correction (10-25% drop)                              │
│  ├── Reduce new position sizes by 50%                                   │
│  ├── Widen grid spacing                                                 │
│  ├── Hold existing positions                                            │
│  └── Optional: Add hedges                                               │
│                                                                          │
│  Tier 3: Severe Drop (25-40% drop)                                      │
│  ├── Stop opening new positions                                         │
│  ├── Preserve capital for recovery                                      │
│  ├── Consider partial position reduction                                │
│  └── Activate hedging if available                                      │
│                                                                          │
│  Tier 4: Crisis Mode (40%+ drop)                                        │
│  ├── Full defensive mode                                                │
│  ├── No new positions                                                   │
│  ├── Evaluate if thesis still valid                                     │
│  └── Prepare for long hold (years)                                      │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.3 Mathematical Framework for Survival

**The Survival Constraint:**

```
Max Position Value × Max Expected Drop < Account Equity × (1 - Margin Requirement)
```

**Example Calculation:**

```
Account: $10,000
Leverage: 1:100
Margin Requirement: 1%
Max Expected Drop: 50% (conservative)

Max Safe Position = $10,000 × (1 - 0.01) / 0.50 = $19,800

With 1:100 leverage, this means:
Max Lots = $19,800 / ($100,000 per lot) = 0.198 lots total across all positions
```

**The fill_up `survive` parameter should be set based on:**

```python
survive_parameter = max_expected_drop_percent + safety_margin

# For gold with 50% max expected drop + 10% safety:
survive = 60  # This means strategy assumes price could drop 60% from entry
```

---

## 5. Turning Drops into Gains

### 5.1 The Grid Trading Advantage

Grid trading (which fill_up implements) naturally benefits from volatility:

```
Price Movement:  ────────────────────────────────────────────────────
                      ╱╲      ╱╲      ╱╲
                     ╱  ╲    ╱  ╲    ╱  ╲
                    ╱    ╲  ╱    ╲  ╱    ╲
                   ╱      ╲╱      ╲╱      ╲
                  ╱                        ╲
                 ╱                          ╲
                ╱                            ╲
               ╱                              ╲
              ╱                                ╲
             ╱                                  ╲
            ╱                                    ╲
           ╱                                      ╲
          ╱                                        ╲
         ╱                                          ╲
        ╱                                            ╲
       ╱                                              ╲
      ╱                                                ╲
     ╱                                                  ╲
    ╱                                                    ╲
   ╱                                                      ╲
  ╱                                                        ╲
 ╱                                                          ╲
╱                                                            ╲

Grid Levels:  ─────────────────────────────────────────────────────
              Buy 1 ────────────────────────────────────────────────
              Buy 2 ────────────────────────────────────────────────
              Buy 3 ────────────────────────────────────────────────
              Buy 4 ────────────────────────────────────────────────
              Buy 5 ────────────────────────────────────────────────

Each bounce = profit on lower grid levels
Each new low = accumulate at better prices
```

### 5.2 The Key Insight: Drops Are Opportunities (If You Survive)

**Traditional View:**
- Drop = Loss
- Recovery = Break-even

**Grid Trading View:**
- Drop = Accumulation at better prices
- Recovery = Profit on accumulated positions

**The Math:**

```
Scenario: Gold drops 30% then recovers

Traditional Buy-and-Hold:
- Buy at $2000, drops to $1400, recovers to $2000
- Result: 0% gain

Grid Trading (simplified):
- Buy 0.1 lot at $2000
- Buy 0.1 lot at $1800 (10% drop)
- Buy 0.1 lot at $1600 (20% drop)
- Buy 0.1 lot at $1400 (30% drop)
- Average entry: $1700
- Recovery to $2000: +17.6% gain on total position
- Plus: Each grid level takes profit on bounces during the drop
```

### 5.3 The Danger: Leverage Amplifies Both Ways

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    LEVERAGE IMPACT ON SURVIVAL                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Leverage    Max Survivable Drop    Risk Level                          │
│  ─────────────────────────────────────────────────                      │
│  1:1         100% (can't blow up)   Very Low                            │
│  1:10        ~90%                   Low                                 │
│  1:50        ~50%                   Medium                              │
│  1:100       ~30%                   High                                │
│  1:500       ~10%                   Extreme                             │
│                                                                          │
│  Note: These are theoretical maximums. Practical limits are lower       │
│  due to margin requirements, spreads, and swap costs.                   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Practical Implementation

### 6.1 Parameter Guidelines for Gold

Based on the controllables framework:

```python
# Conservative settings for gold grid trading
GOLD_CONSERVATIVE = {
    'survive': 5.0,      # Expect up to 50% drop from highest grid entry
    'size': 0.5,         # Half the default position size
    'spacing': 2.0,      # Double the default spacing
    'max_positions': 10, # Limit total grid levels
    'max_margin_pct': 30 # Never use more than 30% of available margin
}

# Moderate settings
GOLD_MODERATE = {
    'survive': 3.5,      # Expect up to 35% drop
    'size': 1.0,         # Default position size
    'spacing': 1.5,      # 1.5x default spacing
    'max_positions': 15,
    'max_margin_pct': 50
}

# Aggressive settings (higher risk)
GOLD_AGGRESSIVE = {
    'survive': 2.5,      # Expect up to 25% drop
    'size': 1.5,         # 1.5x position size
    'spacing': 1.0,      # Default spacing
    'max_positions': 20,
    'max_margin_pct': 70
}
```

### 6.2 Dynamic Adjustment Rules

```python
class GoldGridController:
    def __init__(self, base_params, account_equity):
        self.params = base_params
        self.equity = account_equity
        self.peak_price = None
        self.current_tier = 1
    
    def update_tier(self, current_price):
        if self.peak_price is None:
            self.peak_price = current_price
            return
        
        self.peak_price = max(self.peak_price, current_price)
        drop_pct = (self.peak_price - current_price) / self.peak_price * 100
        
        if drop_pct < 10:
            self.current_tier = 1
        elif drop_pct < 25:
            self.current_tier = 2
        elif drop_pct < 40:
            self.current_tier = 3
        else:
            self.current_tier = 4
    
    def get_adjusted_params(self):
        adjustments = {
            1: {'size_mult': 1.0, 'spacing_mult': 1.0, 'new_positions': True},
            2: {'size_mult': 0.5, 'spacing_mult': 1.5, 'new_positions': True},
            3: {'size_mult': 0.25, 'spacing_mult': 2.0, 'new_positions': False},
            4: {'size_mult': 0.0, 'spacing_mult': 0.0, 'new_positions': False},
        }
        
        adj = adjustments[self.current_tier]
        return {
            'survive': self.params['survive'],
            'size': self.params['size'] * adj['size_mult'],
            'spacing': self.params['spacing'] * adj['spacing_mult'],
            'allow_new_positions': adj['new_positions']
        }
```

### 6.3 Account Protection Mechanisms

```python
class AccountProtector:
    def __init__(self, account_equity, max_drawdown_pct=30):
        self.initial_equity = account_equity
        self.max_drawdown_pct = max_drawdown_pct
        self.peak_equity = account_equity
        self.is_locked = False
    
    def check_and_protect(self, current_equity):
        self.peak_equity = max(self.peak_equity, current_equity)
        current_drawdown = (self.peak_equity - current_equity) / self.peak_equity * 100
        
        if current_drawdown >= self.max_drawdown_pct:
            self.is_locked = True
            return {
                'action': 'LOCK_TRADING',
                'reason': f'Max drawdown {self.max_drawdown_pct}% reached',
                'current_drawdown': current_drawdown,
                'recommendation': 'Close all positions or wait for recovery'
            }
        
        if current_drawdown >= self.max_drawdown_pct * 0.8:
            return {
                'action': 'WARNING',
                'reason': f'Approaching max drawdown ({current_drawdown:.1f}%)',
                'recommendation': 'Reduce position sizes, widen spacing'
            }
        
        return {'action': 'CONTINUE', 'current_drawdown': current_drawdown}
```

---

## 7. Testing the Strategy Against Unknown Drops

### 7.1 Stress Testing Framework

Since we don't know when or how deep drops will be, we test against historical worst cases:

```python
class StressTestFramework:
    # Historical gold stress scenarios
    SCENARIOS = [
        {'name': '1980 Crash', 'drop_pct': 65, 'duration_months': 24, 'recovery_years': 28},
        {'name': '2008 Crisis', 'drop_pct': 30, 'duration_months': 8, 'recovery_years': 1},
        {'name': '2011-2015 Bear', 'drop_pct': 45, 'duration_months': 48, 'recovery_years': 5},
        {'name': '2020 COVID', 'drop_pct': 15, 'duration_months': 1, 'recovery_years': 0.2},
        {'name': '2022 Rate Hikes', 'drop_pct': 22, 'duration_months': 8, 'recovery_years': 1},
        {'name': 'Hypothetical Extreme', 'drop_pct': 70, 'duration_months': 36, 'recovery_years': 10},
    ]
    
    def run_stress_test(self, strategy_params, initial_equity):
        results = []
        
        for scenario in self.SCENARIOS:
            # Simulate the drop
            result = self._simulate_scenario(
                strategy_params,
                initial_equity,
                scenario['drop_pct'],
                scenario['duration_months']
            )
            
            results.append({
                'scenario': scenario['name'],
                'survived': result['final_equity'] > 0,
                'max_drawdown': result['max_drawdown'],
                'final_equity': result['final_equity'],
                'margin_call': result['margin_call_occurred'],
                'recovery_profit': result.get('recovery_profit', 0)
            })
        
        return {
            'results': results,
            'all_survived': all(r['survived'] for r in results),
            'worst_case': min(results, key=lambda x: x['final_equity']),
            'recommendation': self._generate_recommendation(results)
        }
    
    def _generate_recommendation(self, results):
        failed = [r for r in results if not r['survived']]
        if failed:
            return f"FAIL: Strategy would blow up in {len(failed)} scenarios. Reduce leverage or position sizes."
        
        worst = min(results, key=lambda x: x['final_equity'])
        if worst['max_drawdown'] > 50:
            return f"WARNING: Max drawdown {worst['max_drawdown']:.1f}% in {worst['scenario']}. Consider more conservative settings."
        
        return "PASS: Strategy survives all historical stress scenarios."
```

### 7.2 Monte Carlo for Unknown Drops

```python
class UnknownDropSimulator:
    def __init__(self, n_simulations=10000):
        self.n_simulations = n_simulations
    
    def simulate_unknown_drops(self, strategy_params, initial_equity):
        """
        Simulate random drops we haven't seen historically
        """
        results = []
        
        for _ in range(self.n_simulations):
            # Generate random drop characteristics
            drop_pct = np.random.uniform(10, 80)  # 10% to 80% drop
            duration_months = np.random.uniform(1, 60)  # 1 month to 5 years
            volatility = np.random.uniform(0.5, 3.0)  # Volatility multiplier
            
            result = self._simulate_random_drop(
                strategy_params,
                initial_equity,
                drop_pct,
                duration_months,
                volatility
            )
            
            results.append({
                'drop_pct': drop_pct,
                'duration': duration_months,
                'survived': result['survived'],
                'final_equity': result['final_equity']
            })
        
        survival_rate = sum(1 for r in results if r['survived']) / len(results)
        
        return {
            'survival_rate': survival_rate,
            'median_final_equity': np.median([r['final_equity'] for r in results]),
            'worst_5_pct': np.percentile([r['final_equity'] for r in results], 5),
            'recommendation': self._recommend(survival_rate)
        }
    
    def _recommend(self, survival_rate):
        if survival_rate >= 0.99:
            return "EXCELLENT: 99%+ survival rate across random scenarios"
        elif survival_rate >= 0.95:
            return "GOOD: 95%+ survival rate, but consider edge cases"
        elif survival_rate >= 0.90:
            return "MODERATE: 90%+ survival, reduce leverage for safety"
        else:
            return f"POOR: Only {survival_rate*100:.1f}% survival. Significantly reduce risk."
```

---

## 8. Summary: The Controllables-Based Approach

### 8.1 What We Control and How

| Controllable | How to Use It | Impact |
|--------------|---------------|--------|
| Position Size | Set `size` parameter conservatively | Directly affects survival |
| Grid Spacing | Set `spacing` wider in uncertain times | Reduces margin usage |
| Survival Threshold | Set `survive` based on worst-case drop | Determines max exposure |
| Tier System | Automatically adjust based on drawdown | Adapts to market conditions |
| Account Protection | Hard stop at max drawdown % | Prevents total loss |

### 8.2 What We Accept and Prepare For

| Uncontrollable | Preparation | Mitigation |
|----------------|-------------|------------|
| Drop Timing | Always be prepared | Conservative base settings |
| Drop Depth | Stress test against 70%+ drops | Survival-first parameters |
| Drop Duration | Ensure funding for years | Low swap costs, no margin pressure |
| Recovery Time | Accept long holds | Position for eventual profit |

### 8.3 The Core Strategy

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    GOLD GRID TRADING PHILOSOPHY                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  1. SURVIVE FIRST                                                        │
│     └── Size positions so you can survive a 70% drop                    │
│                                                                          │
│  2. ACCUMULATE ON DROPS                                                  │
│     └── Each drop is an opportunity to buy cheaper                      │
│                                                                          │
│  3. PROFIT ON BOUNCES                                                    │
│     └── Grid takes profit on every recovery wave                        │
│                                                                          │
│  4. TRUST THE THESIS                                                     │
│     └── Gold has always recovered; position for the long term           │
│                                                                          │
│  5. ADAPT TO CONDITIONS                                                  │
│     └── Tier system reduces risk as drawdown increases                  │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 9. Implementation Checklist

### Before Going Live:

- [ ] Stress test against all historical gold crashes
- [ ] Run Monte Carlo with 10,000+ random drop scenarios
- [ ] Verify 99%+ survival rate across simulations
- [ ] Set `survive` parameter for 70%+ drop tolerance
- [ ] Implement tier system for dynamic adjustment
- [ ] Set hard account protection at 30% max drawdown
- [ ] Calculate exact position sizes for your account
- [ ] Test with paper trading for 3+ months
- [ ] Start with 25% of planned capital
- [ ] Scale up only after live validation

### Ongoing Monitoring:

- [ ] Track current tier level daily
- [ ] Monitor margin usage vs limits
- [ ] Review drawdown vs protection threshold
- [ ] Adjust parameters if market regime changes
- [ ] Document all parameter changes and reasons

---

## 10. Conclusion

The key insight is that **we can design around uncertainty** by:

1. **Accepting** that we don't know when or how deep drops will be
2. **Controlling** position sizes to survive worst-case scenarios
3. **Structuring** the strategy to profit from volatility (grid trading)
4. **Adapting** dynamically as conditions change (tier system)
5. **Protecting** the account with hard limits (max drawdown stop)

The fill_up strategy is well-suited for this approach because:
- It's a grid strategy that benefits from volatility
- It has margin-aware position sizing built in
- The `survive` parameter directly controls drop tolerance
- It can be enhanced with the tier system and protection mechanisms

**The ultimate goal:** Create a strategy that **cannot blow up** under any realistic scenario, while still capturing the long-term appreciation of gold and profiting from its volatility.

---

*This document is part of the external review for the ctrader-backtest system.*
