# Trading Strategy Design Guidelines

## Core Philosophy

A good strategy **does not bet on the direction of price**. It bets that **price will not stay stationary**.

## Fundamental Principles

### 1. Direction-Agnostic Profit
- Strategy should profit from movement, not direction
- Price goes up → Profit
- Price goes down → Also profit (or survive and prepare for opportunity)
- Price oscillates → Maximum profit

### 2. Resilience to ANY Adverse Movement
- "It can always drop more" - never assume a floor
- Strategy must survive any amount of drawdown
- No single event should cause catastrophic loss
- Design for the worst case, profit in the normal case

### 3. Capital Efficiency
- Use capital efficiently to maximize returns
- Don't over-leverage but don't under-utilize
- Every dollar should be working toward profit

### 4. Simple but Sophisticated Risk Control
- Risk management should be elegant, not complex
- Easy to understand, hard to break
- Proactive (prevent losses) not just reactive (limit losses)

### 5. Turn Adversity into Opportunity
- Crashes should be buying opportunities, not disasters
- Drawdowns should load the spring for future profits
- Anti-fragile: get stronger from stress

### 6. Profit Maximization Within Constraints
- After satisfying all above: maximize profit
- Not "maximize profit at any cost"
- Profit is the goal, but survival is the constraint

---

## Asset-Specific Context

### Gold (XAUUSD)
Historical drivers of upward bias:
- Dollar losing value over time
- Safe haven demand during uncertainty
- Inflation hedge
- Central bank accumulation

### NAS100 (Nasdaq)
Historical drivers of upward bias:
- Driving engine of the economy
- Tech showcase and dominance
- Regular 401k inflows
- Must grow faster than alternatives

### The Risk
- Limited money in the world
- If better investment appears → capital flows out → price drops
- Brokerry policy, financial statements, geopolitics → price drops
- Can always drop more than expected

---

## Strategy Evaluation Criteria

### Must Have (Non-Negotiable)
1. ✅ Survives 50%+ price drops without margin call
2. ✅ Generates profit in sideways markets
3. ✅ Has mechanism to profit from (or survive) downtrends
4. ✅ Simple risk rules that are always followed

### Should Have
1. ⭐ Profits from volatility regardless of direction
2. ⭐ Increases opportunity during drawdowns
3. ⭐ Capital efficiency > 80%
4. ⭐ Recovery time < 2x drawdown duration

### Nice to Have
1. 💡 Anti-fragile (stronger after stress)
2. 💡 Self-adjusting to market conditions
3. 💡 Minimal parameter sensitivity

---

## Strategy Design Directions

### Direction 1: Bidirectional Grid
- Place BUY orders below current price
- Place SELL orders above current price
- Profit from oscillation in either direction
- Net exposure stays balanced

### Direction 2: Anti-Fragile Sizing
- Small positions in normal times
- ADD positions when price moves against (better prices)
- Turn drawdowns into loading opportunities
- Crash = spring loading for future profit

### Direction 3: Dynamic Hedging
- Always maintain both long and short exposure
- Adjust ratio based on price level vs mean
- Net exposure near zero, profit from spread

### Direction 4: Volatility Harvesting
- Profit from bid/ask spread at different levels
- Market-making style
- Collect "oscillation premium"

### Direction 5: Hybrid Approach
- Combine multiple mechanisms
- Different strategies for different market regimes
- Graceful degradation when one approach fails

---

## Anti-Patterns (What NOT to Do)

1. ❌ Bet everything on price going up
2. ❌ Use trailing stops that cause churning
3. ❌ Hold losing positions hoping for recovery
4. ❌ Add to losers without a systematic plan
5. ❌ Ignore drawdown until it's critical
6. ❌ Over-optimize for past data
7. ❌ Assume historical patterns will repeat exactly

---

*These guidelines should inform all strategy development and evaluation.*
