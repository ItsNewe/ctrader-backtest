//+------------------------------------------------------------------+
//|                                              GoldSurvivalGrid.mq5|
//|                                  Copyright 2024, Trading Algos   |
//+------------------------------------------------------------------+
#property copyright "Copyright 2024"
#property link      "https://www.mql5.com"
#property version   "1.08"
#property strict

#include <Trade\Trade.mqh>
#include <Trade\PositionInfo.mqh>
#include <Trade\AccountInfo.mqh>

//--- INPUT PARAMETERS
input group "Survival Settings"
input double InpSurvivePct     = 2.5;      // Price % drop to survive (e.g. 2.5%)

input group "Grid Settings"
input double InpSpacingPoints  = 200;      // Grid Spacing (e.g. 200 = $2.00 on Gold)

//--- GLOBALS
CTrade          m_trade;
CPositionInfo   m_position;
CAccountInfo    m_account;

double   m_tick_size;
double   m_tick_value;
double   m_lot_step;
double   m_lot_min;
double   m_lot_max;

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit() {
    if(!SymbolSelect(_Symbol, true)) return(INIT_FAILED);

    m_trade.SetTypeFillingBySymbol(_Symbol);

    m_tick_size  = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_SIZE);
    m_tick_value = SymbolInfoDouble(_Symbol, SYMBOL_TRADE_TICK_VALUE);
    m_lot_step   = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_STEP);
    m_lot_min    = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MIN);
    m_lot_max    = SymbolInfoDouble(_Symbol, SYMBOL_VOLUME_MAX);

    return(INIT_SUCCEEDED); }

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick() {
    double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
    double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);

    double min_dist_to_grid = DBL_MAX;
    double highest_buy = 0;
    int count = 0;

// 1. Scan positions to find closest trade and highest entry
    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(m_position.SelectByIndex(i)) {
            if(m_position.Symbol() == _Symbol) {
                double open_price = m_position.PriceOpen();
                double dist = MathAbs(ask - open_price);

                if(dist < min_dist_to_grid) min_dist_to_grid = dist;
                if(open_price > highest_buy || highest_buy == 0) highest_buy = open_price;

                count++; } } }

// 2. Entry Condition: Gap filling logic
    if(count == 0 || min_dist_to_grid >= (InpSpacingPoints * _Point)) {
        // Anchor the survival floor to the highest buy ever seen in this grid
        double reference_price = (count == 0) ? ask : highest_buy;

        double dynamic_lot = CalculateDynamicLotSize(ask, reference_price);

        if(dynamic_lot >= m_lot_min) {
            double tp = ask + (InpSpacingPoints * _Point) + (ask - bid);
            double sl = 0;
            m_trade.Buy(dynamic_lot, _Symbol, ask, sl, tp, "AdaptiveGrid"); } } }

//+------------------------------------------------------------------+
//| Calculate Lot Size: Uniform for remaining levels, scales with bal|
//+------------------------------------------------------------------+
double CalculateDynamicLotSize(double current_ask, double reference_price) {
    double balance = AccountInfoDouble(ACCOUNT_BALANCE);
    double so_level = AccountInfoDouble(ACCOUNT_MARGIN_SO_SO) / 100.0;

// 1. Determine the Survival Floor (Fixed relative to highest buy)
    double survival_floor = reference_price * (1.0 - (InpSurvivePct / 100.0));

// 2. Calculate projected loss and margin of EXISTING basket at that floor
    double existing_loss_at_floor = 0;
    double existing_margin = 0;

    for(int i = PositionsTotal() - 1; i >= 0; i--) {
        if(m_position.SelectByIndex(i)) {
            if(m_position.Symbol() == _Symbol) {
                double p_open = m_position.PriceOpen();
                double vol = m_position.Volume();
                existing_loss_at_floor += (p_open - survival_floor) / m_tick_size * m_tick_value * vol;

                double m_pos = 0;
                OrderCalcMargin((ENUM_ORDER_TYPE)m_position.PositionType(), _Symbol, vol, p_open, m_pos);
                existing_margin += m_pos; } } }

// 3. Calculate "Free Survival Cash"
// Cash available for NEW trades = Balance - LossAtFloor - (MarginAtFloor * SO)
    double usable_cash = balance - existing_loss_at_floor - (existing_margin * so_level);

    if(usable_cash <= 0) return 0;

// 4. Calculate how many levels are left down to the floor
    double points_to_floor = (current_ask - survival_floor);
    double spacing_price = InpSpacingPoints * _Point;
    int levels_left = (int)MathCeil(points_to_floor / spacing_price);
    if(levels_left <= 0) levels_left = 1;

// 5. Calculate the "Cost" of 1.0 lot for the remaining levels
// Cost = Sum of losses of all potential levels + Total margin of all levels
    double total_points_of_new_levels = 0;
    for(int i = 0; i < levels_left; i++) {
        double level_price = current_ask - (i * spacing_price);
        if(level_price < survival_floor) break;
        total_points_of_new_levels += (level_price - survival_floor); }

    double loss_cost_per_lot = (total_points_of_new_levels / m_tick_size) * m_tick_value;
    double margin_per_lot = 0;
    double margin_top = 0, margin_bottom = 0;
    OrderCalcMargin(ORDER_TYPE_BUY, _Symbol, 1.0, current_ask, margin_top);
    OrderCalcMargin(ORDER_TYPE_BUY, _Symbol, 1.0, survival_floor, margin_bottom);
    margin_per_lot = (margin_top + margin_bottom) / 2;
    double margin_cost_per_lot = (levels_left * margin_per_lot) * so_level;

// 6. Resulting Lot Size
    double lot_size = usable_cash / (loss_cost_per_lot + margin_cost_per_lot);

// Normalization
    lot_size = MathFloor(lot_size / m_lot_step) * m_lot_step;

    if(lot_size < m_lot_min) return m_lot_min; //0;
    if(lot_size > m_lot_max) lot_size = m_lot_max;

    return NormalizeDouble(lot_size, 2); }
//+------------------------------------------------------------------+
