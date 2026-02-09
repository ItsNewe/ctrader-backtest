//+------------------------------------------------------------------+
//|                                    FillUpAdaptive_Calibrate.mq5  |
//|          Measures market characteristics for parameter selection  |
//|     Run as Script on any chart to get recommended EA parameters   |
//+------------------------------------------------------------------+
#property copyright "Backtest Framework"
#property link      ""
#property version   "1.00"
#property script_show_inputs

//+------------------------------------------------------------------+
//| Input Parameters                                                  |
//+------------------------------------------------------------------+
input int AnalysisDays = 90;          // Days of history to analyze
input bool WriteToFile = true;        // Write results to file

//+------------------------------------------------------------------+
//| Structures                                                        |
//+------------------------------------------------------------------+
struct RangeStats
{
   double median;
   double mean;
   double p10;
   double p25;
   double p75;
   double p90;
   double max;
   int    count;
};

struct SwingPoint
{
   double price;
   datetime time;
   bool   isHigh;
};

//+------------------------------------------------------------------+
//| Main Script Function                                              |
//+------------------------------------------------------------------+
void OnStart()
{
   string symbol = _Symbol;
   double currentPrice = SymbolInfoDouble(symbol, SYMBOL_BID);

   if(currentPrice <= 0)
   {
      Print("ERROR: Cannot get current price for ", symbol);
      return;
   }

   Print("================================================================");
   Print("  FillUpAdaptive Parameter Calibration");
   Print("  Symbol: ", symbol, "  Current Price: ", DoubleToString(currentPrice, (int)SymbolInfoInteger(symbol, SYMBOL_DIGITS)));
   Print("  Analysis Period: ", AnalysisDays, " days");
   Print("================================================================");

   //--- Step 1: Load M1 bar data ---
   MqlRates m1Bars[];
   int barsNeeded = AnalysisDays * 24 * 60;  // M1 bars
   int copied = CopyRates(symbol, PERIOD_M1, 0, barsNeeded, m1Bars);

   if(copied < 1440)  // Need at least 1 day
   {
      Print("ERROR: Insufficient M1 data. Got ", copied, " bars, need at least 1440.");
      Print("  Make sure the chart has enough history loaded.");
      return;
   }

   Print("\n  Loaded ", copied, " M1 bars (", copied / 1440, " days)");
   Print("  From: ", TimeToString(m1Bars[0].time));
   Print("  To:   ", TimeToString(m1Bars[copied-1].time));

   //--- Step 2: Calculate Hourly Ranges ---
   Print("\n--- Hourly Range Analysis ---");

   RangeStats hourly1h = CalcHourlyRanges(m1Bars, copied, 60);    // 1-hour ranges
   RangeStats hourly4h = CalcHourlyRanges(m1Bars, copied, 240);   // 4-hour ranges
   RangeStats daily    = CalcHourlyRanges(m1Bars, copied, 1440);   // Daily ranges

   // Convert to % of price (using average price over period)
   double avgPrice = CalcAveragePrice(m1Bars, copied);
   double hourly1h_pct = hourly1h.median / avgPrice * 100.0;
   double hourly4h_pct = hourly4h.median / avgPrice * 100.0;
   double daily_pct    = daily.median / avgPrice * 100.0;

   Print("  Average price over period: ", DoubleToString(avgPrice, 2));
   Print("");
   Print("  1-Hour Ranges (", hourly1h.count, " samples):");
   Print("    Median: $", DoubleToString(hourly1h.median, 3), " (", DoubleToString(hourly1h_pct, 3), "% of price)");
   Print("    P10-P90: $", DoubleToString(hourly1h.p10, 3), " - $", DoubleToString(hourly1h.p90, 3));
   Print("    Max: $", DoubleToString(hourly1h.max, 3));
   Print("");
   Print("  4-Hour Ranges (", hourly4h.count, " samples):");
   Print("    Median: $", DoubleToString(hourly4h.median, 3), " (", DoubleToString(hourly4h_pct, 3), "% of price)");
   Print("    P10-P90: $", DoubleToString(hourly4h.p10, 3), " - $", DoubleToString(hourly4h.p90, 3));
   Print("    Max: $", DoubleToString(hourly4h.max, 3));
   Print("");
   Print("  Daily Ranges (", daily.count, " samples):");
   Print("    Median: $", DoubleToString(daily.median, 3), " (", DoubleToString(daily_pct, 3), "% of price)");
   Print("    P10-P90: $", DoubleToString(daily.p10, 3), " - $", DoubleToString(daily.p90, 3));
   Print("    Max: $", DoubleToString(daily.max, 3));

   //--- Step 3: Maximum Adverse Move (Peak-to-Trough Decline) ---
   Print("\n--- Maximum Adverse Move Analysis ---");

   double maxDeclinePct = 0;
   double maxDeclineAbs = 0;
   datetime declineStart = 0, declineEnd = 0;
   double declinePeakPrice = 0, declineTroughPrice = 0;

   // Use H1 bars for drawdown analysis (smoother than M1)
   MqlRates h1Bars[];
   int h1Count = CopyRates(symbol, PERIOD_H1, 0, AnalysisDays * 24, h1Bars);

   if(h1Count > 24)
   {
      CalcMaxAdverseMove(h1Bars, h1Count, maxDeclinePct, maxDeclineAbs,
                         declinePeakPrice, declineTroughPrice, declineStart, declineEnd);

      Print("  Max peak-to-trough decline: ", DoubleToString(maxDeclinePct, 2), "%");
      Print("    From $", DoubleToString(declinePeakPrice, 2), " to $", DoubleToString(declineTroughPrice, 2));
      Print("    Period: ", TimeToString(declineStart), " to ", TimeToString(declineEnd));
      Print("    Absolute: $", DoubleToString(maxDeclineAbs, 2));
   }

   // Also check multiple drawdowns (top 3)
   Print("\n  Top 5 drawdowns in period:");
   double ddPcts[];
   double ddPeaks[];
   double ddTroughs[];
   datetime ddStarts[];
   datetime ddEnds[];
   FindTopDrawdowns(h1Bars, h1Count, 5, ddPcts, ddPeaks, ddTroughs, ddStarts, ddEnds);

   for(int i = 0; i < ArraySize(ddPcts); i++)
   {
      Print("    #", i+1, ": -", DoubleToString(ddPcts[i], 2), "%",
            " ($", DoubleToString(ddPeaks[i], 2), " -> $", DoubleToString(ddTroughs[i], 2), ")",
            " [", TimeToString(ddStarts[i], TIME_DATE), " to ", TimeToString(ddEnds[i], TIME_DATE), "]");
   }

   //--- Step 4: Oscillation Frequency Analysis ---
   Print("\n--- Oscillation Frequency Analysis ---");

   double swingThresholdPct = 0.15;  // 0.15% reversal = new swing
   double swingThreshold = avgPrice * swingThresholdPct / 100.0;

   int totalSwings = 0;
   double avgSwingAmplitude = 0;
   double avgSwingDurationMin = 0;
   double medianSwingAmplitude = 0;

   AnalyzeSwings(m1Bars, copied, swingThreshold, totalSwings, avgSwingAmplitude,
                 avgSwingDurationMin, medianSwingAmplitude);

   double swingsPerDay = (double)totalSwings / ((double)copied / 1440.0);
   double swingAmplPct = medianSwingAmplitude / avgPrice * 100.0;

   Print("  Swing threshold: ", DoubleToString(swingThresholdPct, 2), "% ($", DoubleToString(swingThreshold, 3), ")");
   Print("  Total swings detected: ", totalSwings);
   Print("  Swings per day: ", DoubleToString(swingsPerDay, 1));
   Print("  Median swing amplitude: $", DoubleToString(medianSwingAmplitude, 3),
         " (", DoubleToString(swingAmplPct, 3), "% of price)");
   Print("  Average swing duration: ", DoubleToString(avgSwingDurationMin, 1), " minutes");

   //--- Step 5: Trend Analysis ---
   Print("\n--- Trend Analysis ---");

   double totalChangePct = (m1Bars[copied-1].close - m1Bars[0].open) / m1Bars[0].open * 100.0;
   Print("  Total price change over period: ", DoubleToString(totalChangePct, 2), "%");

   // Monthly trend breakdown
   Print("  Monthly breakdown:");
   int monthStart = 0;
   for(int i = 1; i < copied; i++)
   {
      MqlDateTime dt1, dt2;
      TimeToStruct(m1Bars[i-1].time, dt1);
      TimeToStruct(m1Bars[i].time, dt2);

      if(dt1.mon != dt2.mon || i == copied - 1)
      {
         double monthChange = (m1Bars[i-1].close - m1Bars[monthStart].open) / m1Bars[monthStart].open * 100.0;
         MqlDateTime dtStart;
         TimeToStruct(m1Bars[monthStart].time, dtStart);
         Print("    ", dtStart.year, ".", StringFormat("%02d", dtStart.mon),
               ": ", (monthChange >= 0 ? "+" : ""), DoubleToString(monthChange, 2), "%");
         monthStart = i;
      }
   }

   //--- Step 6: Parameter Recommendations ---
   Print("\n================================================================");
   Print("  RECOMMENDED PARAMETERS FOR FillUpAdaptive_v4");
   Print("================================================================");

   // SurvivePct: 2x the max adverse move (safety margin)
   double recSurvive = MathMax(maxDeclinePct * 2.0, 15.0);
   recSurvive = MathMin(recSurvive, 50.0);
   // Round to nearest 5
   recSurvive = MathRound(recSurvive / 5.0) * 5.0;

   // TypicalVolPct: Use 1h median range as % of price
   double recTypVol1h = hourly1h_pct;
   double recTypVol4h = hourly4h_pct;

   // BaseSpacingPct: ~50% of median swing amplitude
   double recSpacing = swingAmplPct * 0.5;
   recSpacing = MathMax(0.5, MathMin(5.0, recSpacing));
   // Round to nearest 0.5
   recSpacing = MathRound(recSpacing * 2.0) / 2.0;

   // Lookback: Based on swing duration
   double recLookback = 1.0;
   if(avgSwingDurationMin > 120) recLookback = 4.0;
   else if(avgSwingDurationMin > 60) recLookback = 2.0;
   else if(avgSwingDurationMin > 30) recLookback = 1.0;
   else recLookback = 0.5;

   Print("");
   Print("  +------------------------------------------+");
   Print("  | Parameter              | Value           |");
   Print("  +------------------------------------------+");
   Print("  | SurvivePct             | ", StringFormat("%-15s", DoubleToString(recSurvive, 1)), " |");
   Print("  | BaseSpacingPct         | ", StringFormat("%-15s", DoubleToString(recSpacing, 1)), " |");
   Print("  | VolatilityLookbackHours| ", StringFormat("%-15s", DoubleToString(recLookback, 1)), " |");
   Print("  | TypicalVolPct (1h lb)  | ", StringFormat("%-15s", DoubleToString(recTypVol1h, 3)), " |");
   Print("  | TypicalVolPct (4h lb)  | ", StringFormat("%-15s", DoubleToString(recTypVol4h, 3)), " |");
   Print("  +------------------------------------------+");
   Print("");
   Print("  Reasoning:");
   Print("    SurvivePct = ", DoubleToString(recSurvive, 0), "%: ",
         "2x max adverse move (", DoubleToString(maxDeclinePct, 1), "%) for safety margin");
   Print("    BaseSpacingPct = ", DoubleToString(recSpacing, 1), "%: ",
         "50% of median swing (", DoubleToString(swingAmplPct, 3), "%)");
   Print("    Lookback = ", DoubleToString(recLookback, 1), "h: ",
         "avg swing duration ", DoubleToString(avgSwingDurationMin, 0), " min");
   Print("    TypicalVolPct: Use value matching your lookback setting");

   //--- Step 7: Conservative / Balanced / Aggressive variants ---
   Print("\n  --- Preset Variants ---");
   Print("");

   double conserveSurvive = MathMin(recSurvive * 1.4, 50.0);
   conserveSurvive = MathRound(conserveSurvive / 5.0) * 5.0;
   double conserveSpacing = MathMin(recSpacing * 1.5, 8.0);
   conserveSpacing = MathRound(conserveSpacing * 2.0) / 2.0;

   double aggressSurvive = MathMax(recSurvive * 0.7, 15.0);
   aggressSurvive = MathRound(aggressSurvive / 5.0) * 5.0;
   double aggressSpacing = MathMax(recSpacing * 0.7, 0.5);
   aggressSpacing = MathRound(aggressSpacing * 2.0) / 2.0;

   Print("  Conservative (lower DD, lower return):");
   Print("    SurvivePct=", DoubleToString(conserveSurvive, 0),
         ", SpacingPct=", DoubleToString(conserveSpacing, 1),
         ", Lookback=", DoubleToString(recLookback, 1), "h",
         ", TypVol=", DoubleToString(recTypVol1h, 3), "%");
   Print("");
   Print("  Balanced (recommended):");
   Print("    SurvivePct=", DoubleToString(recSurvive, 0),
         ", SpacingPct=", DoubleToString(recSpacing, 1),
         ", Lookback=", DoubleToString(recLookback, 1), "h",
         ", TypVol=", DoubleToString(recTypVol1h, 3), "%");
   Print("");
   Print("  Aggressive (higher return, higher DD):");
   Print("    SurvivePct=", DoubleToString(aggressSurvive, 0),
         ", SpacingPct=", DoubleToString(aggressSpacing, 1),
         ", Lookback=", DoubleToString(recLookback, 1), "h",
         ", TypVol=", DoubleToString(recTypVol1h, 3), "%");

   //--- Write to file if requested ---
   if(WriteToFile)
   {
      string fileName = "FillUp_Calibration_" + symbol + ".txt";
      int handle = FileOpen(fileName, FILE_WRITE | FILE_TXT | FILE_ANSI);
      if(handle != INVALID_HANDLE)
      {
         FileWriteString(handle, "# FillUpAdaptive Calibration Results\n");
         FileWriteString(handle, "# Symbol: " + symbol + "\n");
         FileWriteString(handle, "# Date: " + TimeToString(TimeCurrent()) + "\n");
         FileWriteString(handle, "# Analysis Period: " + IntegerToString(AnalysisDays) + " days\n");
         FileWriteString(handle, "# Average Price: $" + DoubleToString(avgPrice, 2) + "\n");
         FileWriteString(handle, "#\n");
         FileWriteString(handle, "# === Measured Characteristics ===\n");
         FileWriteString(handle, "HourlyRangeMedian_pct=" + DoubleToString(hourly1h_pct, 4) + "\n");
         FileWriteString(handle, "FourHourRangeMedian_pct=" + DoubleToString(hourly4h_pct, 4) + "\n");
         FileWriteString(handle, "DailyRangeMedian_pct=" + DoubleToString(daily_pct, 4) + "\n");
         FileWriteString(handle, "MaxAdverseMove_pct=" + DoubleToString(maxDeclinePct, 2) + "\n");
         FileWriteString(handle, "SwingsPerDay=" + DoubleToString(swingsPerDay, 1) + "\n");
         FileWriteString(handle, "MedianSwingAmplitude_pct=" + DoubleToString(swingAmplPct, 4) + "\n");
         FileWriteString(handle, "AvgSwingDuration_min=" + DoubleToString(avgSwingDurationMin, 1) + "\n");
         FileWriteString(handle, "TotalChangePct=" + DoubleToString(totalChangePct, 2) + "\n");
         FileWriteString(handle, "#\n");
         FileWriteString(handle, "# === Recommended Parameters ===\n");
         FileWriteString(handle, "SurvivePct=" + DoubleToString(recSurvive, 1) + "\n");
         FileWriteString(handle, "BaseSpacingPct=" + DoubleToString(recSpacing, 1) + "\n");
         FileWriteString(handle, "VolatilityLookbackHours=" + DoubleToString(recLookback, 1) + "\n");
         FileWriteString(handle, "TypicalVolPct_1h=" + DoubleToString(recTypVol1h, 4) + "\n");
         FileWriteString(handle, "TypicalVolPct_4h=" + DoubleToString(recTypVol4h, 4) + "\n");
         FileWriteString(handle, "#\n");
         FileWriteString(handle, "# === Conservative Preset ===\n");
         FileWriteString(handle, "# SurvivePct=" + DoubleToString(conserveSurvive, 1) + "\n");
         FileWriteString(handle, "# BaseSpacingPct=" + DoubleToString(conserveSpacing, 1) + "\n");
         FileWriteString(handle, "#\n");
         FileWriteString(handle, "# === Aggressive Preset ===\n");
         FileWriteString(handle, "# SurvivePct=" + DoubleToString(aggressSurvive, 1) + "\n");
         FileWriteString(handle, "# BaseSpacingPct=" + DoubleToString(aggressSpacing, 1) + "\n");
         FileClose(handle);
         Print("\n  Results saved to: MQL5/Files/", fileName);
      }
   }

   Print("\n================================================================");
   Print("  CALIBRATION COMPLETE");
   Print("================================================================");
}

//+------------------------------------------------------------------+
//| Calculate ranges over fixed bar windows                           |
//+------------------------------------------------------------------+
RangeStats CalcHourlyRanges(const MqlRates &bars[], int count, int windowBars)
{
   RangeStats stats = {};
   double ranges[];

   int numWindows = count / windowBars;
   if(numWindows < 2) { stats.count = 0; return stats; }

   ArrayResize(ranges, numWindows);
   int validCount = 0;

   for(int w = 0; w < numWindows; w++)
   {
      int startIdx = w * windowBars;
      double high = bars[startIdx].high;
      double low = bars[startIdx].low;

      for(int i = 1; i < windowBars && (startIdx + i) < count; i++)
      {
         if(bars[startIdx + i].high > high) high = bars[startIdx + i].high;
         if(bars[startIdx + i].low < low) low = bars[startIdx + i].low;
      }

      double range = high - low;
      if(range > 0)
      {
         ranges[validCount] = range;
         validCount++;
      }
   }

   if(validCount < 2) { stats.count = 0; return stats; }

   ArrayResize(ranges, validCount);
   ArraySort(ranges);

   stats.count = validCount;
   stats.median = ranges[validCount / 2];
   stats.p10 = ranges[(int)(validCount * 0.10)];
   stats.p25 = ranges[(int)(validCount * 0.25)];
   stats.p75 = ranges[(int)(validCount * 0.75)];
   stats.p90 = ranges[(int)(validCount * 0.90)];
   stats.max = ranges[validCount - 1];

   double sum = 0;
   for(int i = 0; i < validCount; i++) sum += ranges[i];
   stats.mean = sum / validCount;

   return stats;
}

//+------------------------------------------------------------------+
//| Calculate average price over period                               |
//+------------------------------------------------------------------+
double CalcAveragePrice(const MqlRates &bars[], int count)
{
   double sum = 0;
   int step = MathMax(1, count / 1000);  // Sample 1000 points for speed
   int samples = 0;

   for(int i = 0; i < count; i += step)
   {
      sum += (bars[i].high + bars[i].low) / 2.0;
      samples++;
   }

   return (samples > 0) ? sum / samples : 0;
}

//+------------------------------------------------------------------+
//| Calculate maximum peak-to-trough decline                          |
//+------------------------------------------------------------------+
void CalcMaxAdverseMove(const MqlRates &bars[], int count,
                        double &maxPct, double &maxAbs,
                        double &peakPrice, double &troughPrice,
                        datetime &peakTime, datetime &troughTime)
{
   maxPct = 0;
   maxAbs = 0;
   double runningPeak = bars[0].high;
   datetime runningPeakTime = bars[0].time;

   for(int i = 1; i < count; i++)
   {
      if(bars[i].high > runningPeak)
      {
         runningPeak = bars[i].high;
         runningPeakTime = bars[i].time;
      }

      double decline = runningPeak - bars[i].low;
      double declinePct = decline / runningPeak * 100.0;

      if(declinePct > maxPct)
      {
         maxPct = declinePct;
         maxAbs = decline;
         peakPrice = runningPeak;
         troughPrice = bars[i].low;
         peakTime = runningPeakTime;
         troughTime = bars[i].time;
      }
   }
}

//+------------------------------------------------------------------+
//| Find top N drawdowns (non-overlapping)                            |
//+------------------------------------------------------------------+
void FindTopDrawdowns(const MqlRates &bars[], int count, int topN,
                      double &pcts[], double &peaks[], double &troughs[],
                      datetime &starts[], datetime &ends[])
{
   // Simple approach: divide into monthly segments and find max DD per segment
   ArrayResize(pcts, 0);
   ArrayResize(peaks, 0);
   ArrayResize(troughs, 0);
   ArrayResize(starts, 0);
   ArrayResize(ends, 0);

   // Find all local drawdowns > 2%
   struct DrawdownInfo
   {
      double pct;
      double peak;
      double trough;
      datetime start;
      datetime end;
   };
   DrawdownInfo dds[];
   int ddCount = 0;

   double runningPeak = bars[0].high;
   datetime peakTime = bars[0].time;
   bool inDrawdown = false;
   double ddStart = 0;
   datetime ddStartTime = 0;

   for(int i = 1; i < count; i++)
   {
      if(bars[i].high > runningPeak)
      {
         // New peak - if we were in a drawdown, record it
         if(inDrawdown && ddCount > 0)
         {
            // Check if recovery from last DD
         }
         runningPeak = bars[i].high;
         peakTime = bars[i].time;
         inDrawdown = false;
      }

      double currentDD = (runningPeak - bars[i].low) / runningPeak * 100.0;

      if(currentDD > 2.0 && !inDrawdown)
      {
         inDrawdown = true;
         ddStart = runningPeak;
         ddStartTime = peakTime;
      }

      if(inDrawdown)
      {
         // Check if recovery (price back within 1% of peak)
         if(bars[i].high > runningPeak * 0.99)
         {
            // Find the worst point in this drawdown
            double worstPrice = bars[i].low;
            datetime worstTime = bars[i].time;
            for(int j = (int)((ddStartTime - bars[0].time) / PeriodSeconds(PERIOD_H1)); j <= i; j++)
            {
               if(j >= 0 && j < count && bars[j].low < worstPrice)
               {
                  worstPrice = bars[j].low;
                  worstTime = bars[j].time;
               }
            }

            double ddPct = (ddStart - worstPrice) / ddStart * 100.0;
            if(ddPct > 2.0)
            {
               ArrayResize(dds, ddCount + 1);
               dds[ddCount].pct = ddPct;
               dds[ddCount].peak = ddStart;
               dds[ddCount].trough = worstPrice;
               dds[ddCount].start = ddStartTime;
               dds[ddCount].end = worstTime;
               ddCount++;
            }

            inDrawdown = false;
            runningPeak = bars[i].high;
            peakTime = bars[i].time;
         }
      }
   }

   // Handle any ongoing drawdown at end of data
   if(inDrawdown)
   {
      double worstPrice = bars[count-1].low;
      datetime worstTime = bars[count-1].time;
      for(int j = 0; j < count; j++)
      {
         if(bars[j].time >= ddStartTime && bars[j].low < worstPrice)
         {
            worstPrice = bars[j].low;
            worstTime = bars[j].time;
         }
      }
      double ddPct = (ddStart - worstPrice) / ddStart * 100.0;
      if(ddPct > 2.0)
      {
         ArrayResize(dds, ddCount + 1);
         dds[ddCount].pct = ddPct;
         dds[ddCount].peak = ddStart;
         dds[ddCount].trough = worstPrice;
         dds[ddCount].start = ddStartTime;
         dds[ddCount].end = worstTime;
         ddCount++;
      }
   }

   // Sort by severity and take top N
   // Simple bubble sort (small array)
   for(int i = 0; i < ddCount - 1; i++)
   {
      for(int j = 0; j < ddCount - i - 1; j++)
      {
         if(dds[j].pct < dds[j+1].pct)
         {
            DrawdownInfo temp = dds[j];
            dds[j] = dds[j+1];
            dds[j+1] = temp;
         }
      }
   }

   int resultCount = MathMin(topN, ddCount);
   ArrayResize(pcts, resultCount);
   ArrayResize(peaks, resultCount);
   ArrayResize(troughs, resultCount);
   ArrayResize(starts, resultCount);
   ArrayResize(ends, resultCount);

   for(int i = 0; i < resultCount; i++)
   {
      pcts[i] = dds[i].pct;
      peaks[i] = dds[i].peak;
      troughs[i] = dds[i].trough;
      starts[i] = dds[i].start;
      ends[i] = dds[i].end;
   }
}

//+------------------------------------------------------------------+
//| Analyze price swings (oscillation characteristics)                |
//+------------------------------------------------------------------+
void AnalyzeSwings(const MqlRates &bars[], int count, double threshold,
                   int &totalSwings, double &avgAmplitude,
                   double &avgDurationMin, double &medianAmplitude)
{
   totalSwings = 0;
   avgAmplitude = 0;
   avgDurationMin = 0;
   medianAmplitude = 0;

   if(count < 10) return;

   // Track swings using close prices
   bool inUpswing = true;
   double extremePrice = bars[0].close;
   int extremeIdx = 0;

   double amplitudes[];
   double durations[];
   int swingCount = 0;

   for(int i = 1; i < count; i++)
   {
      double price = bars[i].close;

      if(inUpswing)
      {
         if(price > extremePrice)
         {
            extremePrice = price;
            extremeIdx = i;
         }
         else if(price < extremePrice - threshold)
         {
            // Upswing completed
            double amplitude = extremePrice - price;
            double duration = (double)(i - extremeIdx);  // in minutes (M1 bars)

            ArrayResize(amplitudes, swingCount + 1);
            ArrayResize(durations, swingCount + 1);
            amplitudes[swingCount] = amplitude;
            durations[swingCount] = duration;
            swingCount++;

            inUpswing = false;
            extremePrice = price;
            extremeIdx = i;
         }
      }
      else
      {
         if(price < extremePrice)
         {
            extremePrice = price;
            extremeIdx = i;
         }
         else if(price > extremePrice + threshold)
         {
            // Downswing completed
            double amplitude = price - extremePrice;
            double duration = (double)(i - extremeIdx);

            ArrayResize(amplitudes, swingCount + 1);
            ArrayResize(durations, swingCount + 1);
            amplitudes[swingCount] = amplitude;
            durations[swingCount] = duration;
            swingCount++;

            inUpswing = true;
            extremePrice = price;
            extremeIdx = i;
         }
      }
   }

   totalSwings = swingCount;
   if(swingCount == 0) return;

   // Calculate averages
   double sumAmp = 0, sumDur = 0;
   for(int i = 0; i < swingCount; i++)
   {
      sumAmp += amplitudes[i];
      sumDur += durations[i];
   }
   avgAmplitude = sumAmp / swingCount;
   avgDurationMin = sumDur / swingCount;

   // Calculate median amplitude
   ArraySort(amplitudes);
   medianAmplitude = amplitudes[swingCount / 2];
}
//+------------------------------------------------------------------+
