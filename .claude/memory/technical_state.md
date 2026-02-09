# Technical State - Session 2026-01-29

## Multi-Instrument Investigation Results

### XAGUSD (Silver) - RECOMMENDED
- **Best return**: s19% sp2.0% lb1 → 73.68x 2-year, 4.19x ratio
- **Best stability**: s19% sp1.5% lb1 → 33.28x 2-year, 3.13x ratio
- Tick growth 2024→2025: 1.32x (very stable)
- s18% stops out in 2024 - must use s19%+
- s19% sp3.0% also stops out - spacing can't exceed ~2.5%
- Uses pct_spacing mode (critical - price tripled $29→$96)

### XPTUSD (Platinum) - USE WITH CAUTION
- **Best config**: s18% sp1.5% lb1 → 30.7x 2-year, but 17x regime ratio
- Tick growth 2024→2025: 2.22x (structural change)
- 2024: only 1.1-1.4x returns
- 2025: 4.7-22.7x returns (price doubled, liquidity changed)
- **Problem**: Performance depends heavily on market regime

### XPDUSD (Palladium) - MARGINAL
- Only survive=25% is safe (7/8 configs survive)
- survive=20% has 99.4% max DD (near stop-out)
- Best safe: s25% sp3% lb1 → 5.52x, 59.7% DD

### ETHUSD (Ethereum) - NOT VIABLE
- All 32 configs stopped out
- Different market microstructure (24/7, momentum-driven)

### XAUUSD (Gold) - BASELINE
- 6.6x (2025), 2.1x (2024), 3.1x ratio
- CombinedJu P1_M3: 126x 2-year
- Well-documented in CLAUDE.md

## Ranking by Risk-Adjusted Multi-Year Performance

| Rank | Instrument | 2-Year | Ratio | Recommendation |
|------|------------|--------|-------|----------------|
| 1 | XAGUSD | 73.68x | 4.19x | PRIMARY (with gold) |
| 2 | XAUUSD | ~14-126x | 3.1x | PRIMARY (CombinedJu) |
| 3 | XPTUSD | 30.7x | 17x | SECONDARY (regime-dependent) |
| 4 | XPDUSD | 5.52x | N/A | AVOID (marginal safety) |
| 5 | ETHUSD | 0x | N/A | AVOID (100% failure) |

## Outstanding Technical Work

- [ ] Update CLAUDE.md with silver findings
- [ ] Create silver EA preset files
- [ ] Document the "hidden variable" (tick density) discovery
- [ ] Consider portfolio allocation across gold + silver

## Key Insight: Regime ≠ Price

The 2024→2025 regime difference is NOT just about price:
- Gold: price +53%, oscillations +200% (3x)
- Platinum: price +100%, ticks +122%, ratio 17x
- Silver: price +230%, ticks +32%, ratio 4x

**Silver's stability comes from its market microstructure remaining consistent** despite massive price change. This is the "hidden variable" - liquidity/tick density stability matters more than price stability.
