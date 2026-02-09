//+------------------------------------------------------------------+
//|                                              CycleSurfer_EA.mq5  |
//|                        Based on Ehlers Roofing Filter Strategy   |
//|                                  "The Physics of Market Cycles"  |
//+------------------------------------------------------------------+
#property copyright "Kapitza Implementation"
#property link      "https://www.mql5.com"
#property version   "1.00"

#include <Trade\Trade.mqh>

// --- Inputs ---
input double InpLotSize    = 0.1;   // Fixed Lot Size
input int    InpHP_Period  = 60;    // High Pass Period (Trend Removal) - Tuned for Swing
input int    InpSS_Period  = 10;    // Super Smoother Period (Noise Removal)

// --- Globals ---
CTrade Trade;
int    handle_ma;
double BufferHP[];   // High Pass Buffer
double BufferSS[];   // Super Smoother (The Cycle)
double ClosePrice[]; // Price Buffer

//+------------------------------------------------------------------+
//| Expert initialization function                                   |
//+------------------------------------------------------------------+
int OnInit()
{
   // Set arrays as series (index 0 is the newest candle)
   ArraySetAsSeries(BufferHP, true);
   ArraySetAsSeries(BufferSS, true);
   ArraySetAsSeries(ClosePrice, true);
   
   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| Expert tick function                                             |
//+------------------------------------------------------------------+
void OnTick()
{
   // 1. Data Retrieval
   int bars_needed = InpHP_Period * 2;
   if(Bars(_Symbol, _Period) < bars_needed) return;
   
   // Copy Close prices (Get enough history to stabilize the filter)
   int copied = CopyClose(_Symbol, _Period, 0, bars_needed, ClosePrice);
   if(copied < bars_needed) return;
   
   // Resize internal buffers
   ArrayResize(BufferHP, copied);
   ArrayResize(BufferSS, copied);
   
   // 2. Calculate Ehlers Roofing Filter
   // We calculate from oldest to newest, but store in Series arrays (0=Newest)
   // NOTE: Since we mapped arrays as Series, Index [copied-1] is oldest.
   
   CalculateRoofingFilter(copied);

   // 3. Signal Logic
   // We look for Turns in the Cycle (BufferSS)
   // Signal is confirmed on the CLOSED bar (Index 1), not forming bar (Index 0)
   
   double cycle_curr = BufferSS[1]; // Yesterday/Closed Candle
   double cycle_prev = BufferSS[2]; // Day before
   double cycle_old  = BufferSS[3]; 
   
   // Detect Trough (Turn UP)
   bool signal_buy = (cycle_curr > cycle_prev) && (cycle_prev < cycle_old);
   
   // Detect Peak (Turn DOWN)
   bool signal_sell = (cycle_curr < cycle_prev) && (cycle_prev > cycle_old);

   // 4. Execution
   if(signal_buy)
   {
      // Check if we are already Long
      if(!PositionSelect(_Symbol) || PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_SELL)
      {
         Trade.PositionClose(_Symbol); // Close Short if exists
         Trade.Buy(InpLotSize, _Symbol, 0, 0, 0, "Cycle Surfer Buy");
      }
   }
   
   if(signal_sell)
   {
      // Check if we are already Short
      if(!PositionSelect(_Symbol) || PositionGetInteger(POSITION_TYPE) == POSITION_TYPE_BUY)
      {
         Trade.PositionClose(_Symbol); // Close Long if exists
         Trade.Sell(InpLotSize, _Symbol, 0, 0, 0, "Cycle Surfer Sell");
      }
   }
}

//+------------------------------------------------------------------+
//| Calculation Engine                                               |
//+------------------------------------------------------------------+
void CalculateRoofingFilter(int total_bars)
{
   // --- Constants for High Pass ---
   double angle_hp = 0.707 * 2.0 * M_PI / InpHP_Period;
   double alpha1   = (MathCos(angle_hp) + MathSin(angle_hp) - 1.0) / MathCos(angle_hp);
   double c_hp_1   = (1.0 - alpha1 / 2.0) * (1.0 - alpha1 / 2.0);
   double c_hp_2   = 2.0 * (1.0 - alpha1);
   double c_hp_3   = -(1.0 - alpha1) * (1.0 - alpha1);
   
   // --- Constants for Super Smoother ---
   double angle_ss = 1.414 * M_PI / InpSS_Period;
   double a1       = MathExp(-angle_ss);
   double b1       = 2.0 * a1 * MathCos(angle_ss);
   double c2       = b1;
   double c3       = -a1 * a1;
   double c1       = 1.0 - c2 - c3;
   
   // Calculation Loop (Forward: Oldest -> Newest)
   // Because Arrays are AS_SERIES, accessing them with [i] where i goes 0..total 
   // requires converting index to Series index.
   // Let's use standard loops and map index: 
   // Real Index k: 0 (Oldest) to total-1 (Newest)
   // Series Index: total-1-k
   
   for(int k = 0; k < total_bars; k++)
   {
      int i = total_bars - 1 - k; // Series Index (i=0 is newest)
      
      // Initialize start of history with 0 to avoid garbage
      if(k < 2) 
      {
         BufferHP[i] = 0.0;
         BufferSS[i] = 0.0;
         continue;
      }
      
      // 1. High Pass Filter (De-Trend)
      // HP[i] = c_hp_1*(Close[i] - 2*Close[i+1] + Close[i+2]) + c_hp_2*HP[i+1] + c_hp_3*HP[i+2]
      // Note: +1 and +2 are 'older' in Series indexing
      BufferHP[i] = c_hp_1 * (ClosePrice[i] - 2*ClosePrice[i+1] + ClosePrice[i+2]) 
                  + c_hp_2 * BufferHP[i+1] 
                  + c_hp_3 * BufferHP[i+2];
                  
      // 2. Super Smoother (De-Noise)
      // SS[i] = c1*(HP[i] + HP[i+1])/2 + c2*SS[i+1] + c3*SS[i+2]
      BufferSS[i] = c1 * (BufferHP[i] + BufferHP[i+1]) / 2.0
                  + c2 * BufferSS[i+1]
                  + c3 * BufferSS[i+2];
   }
}