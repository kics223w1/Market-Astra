//+------------------------------------------------------------------+
//|                                                  MarketAstra.mq5 |
//|     Send candle, price, and quantitative trend data              |
//+------------------------------------------------------------------+
#property strict

input string ServerHost = "http://127.0.0.1";        // Host
input int    ServerPort = 3456;                      // Port
input string BotName    = "Market Astro";            // Bot name

// Track current chart timeframe
ENUM_TIMEFRAMES gChartTimeframe = (ENUM_TIMEFRAMES)0;

datetime lastCandleTime     = 0;
datetime lastPriceSentTime  = 0;
datetime lastTrendSentTime  = 0;
double   lastPrice          = 0;

// Indicator handles
int hEMA20 = INVALID_HANDLE;
int hEMA50 = INVALID_HANDLE;
int hRSI14 = INVALID_HANDLE;
int hADX14 = INVALID_HANDLE;
int hATR14 = INVALID_HANDLE;

// Recreate all indicator handles for the given timeframe
bool RecreateIndicators(ENUM_TIMEFRAMES tf)
{
   // Release old if any
   if(hEMA20 != INVALID_HANDLE) IndicatorRelease(hEMA20);
   if(hEMA50 != INVALID_HANDLE) IndicatorRelease(hEMA50);
   if(hRSI14 != INVALID_HANDLE) IndicatorRelease(hRSI14);
   if(hADX14 != INVALID_HANDLE) IndicatorRelease(hADX14);
   if(hATR14 != INVALID_HANDLE) IndicatorRelease(hATR14);

   hEMA20 = iMA(_Symbol, tf, 20, 0, MODE_EMA, PRICE_CLOSE);
   hEMA50 = iMA(_Symbol, tf, 50, 0, MODE_EMA, PRICE_CLOSE);
   hRSI14 = iRSI(_Symbol, tf, 14, PRICE_CLOSE);
   hADX14 = iADX(_Symbol, tf, 14);
   hATR14 = iATR(_Symbol, tf, 14);

   if(hEMA20==INVALID_HANDLE || hEMA50==INVALID_HANDLE || hRSI14==INVALID_HANDLE ||
      hADX14==INVALID_HANDLE || hATR14==INVALID_HANDLE)
   {
      Print("❌ Failed to create indicator handles. Error: ", GetLastError());
      return false;
   }
   return true;
}

//+------------------------------------------------------------------+
//| Helpers                                                          |
//+------------------------------------------------------------------+
double PipFactor()
{
   // Convert price delta -> pips
   // For 5-digit (or 3 for JPY), 1 pip = 10 points
   int digits = (int)SymbolInfoInteger(_Symbol, SYMBOL_DIGITS);
   double point = SymbolInfoDouble(_Symbol, SYMBOL_POINT);
   if(digits == 3 || digits == 5) return 1.0 / (point * 10.0);
   return 1.0 / point;
}

string TimeframeStr(ENUM_TIMEFRAMES tf)
{
   switch(tf)
   {
      case PERIOD_M1:  return "M1";
      case PERIOD_M5:  return "M5";
      case PERIOD_M15: return "M15";
      case PERIOD_M30: return "M30";
      case PERIOD_H1:  return "H1";
      case PERIOD_H4:  return "H4";
      case PERIOD_D1:  return "D1";
      default:         return IntegerToString(tf);
   }
}

// Build an initialization message sent once on EA start
string BuildInitMessage()
{
   double bid = SymbolInfoDouble(_Symbol, SYMBOL_BID);
   double ask = SymbolInfoDouble(_Symbol, SYMBOL_ASK);
   double spread_pips = (ask - bid) * PipFactor();

   string msg = StringFormat(
      "Init Snapshot\n"
      "Symbol: %s\n"
      "Timeframe: %s\n"
      "Time: %s\n"
      "Bid: %.5f\n"
      "Ask: %.5f\n"
      "Spread (pips): %.1f\n"
      "Server: %s\n"
      "Account Currency: %s",
      _Symbol,
      TimeframeStr(gChartTimeframe),
      TimeToString(TimeCurrent(), TIME_DATE | TIME_MINUTES),
      bid,
      ask,
      spread_pips,
      AccountInfoString(ACCOUNT_SERVER),
      AccountInfoString(ACCOUNT_CURRENCY)
   );

   return msg;
}

//+------------------------------------------------------------------+
//| Expert initialization                                            |
//+------------------------------------------------------------------+
int OnInit()
{
   Print("MarketAstra EA initialized ✅");

   gChartTimeframe = (ENUM_TIMEFRAMES)_Period;
   lastCandleTime     = iTime(_Symbol, gChartTimeframe, 0);
   lastPrice          = iClose(_Symbol, gChartTimeframe, 0);
   lastPriceSentTime  = TimeCurrent();
   lastTrendSentTime  = TimeCurrent();

   // Create indicator handles (chart timeframe)
   if(!RecreateIndicators(gChartTimeframe))
      return(INIT_FAILED);

    // Send an initialization snapshot to the bot endpoint
    string initData = BuildInitMessage();
    SendBotData(initData);

   return(INIT_SUCCEEDED);
}

//+------------------------------------------------------------------+
//| OnTick handler                                                   |
//+------------------------------------------------------------------+
void OnTick()
{
   datetime now = TimeCurrent();

   // Detect timeframe change on the chart and rebuild indicators if needed
   ENUM_TIMEFRAMES tfNow = (ENUM_TIMEFRAMES)_Period;
   if(tfNow != gChartTimeframe)
   {
      gChartTimeframe = tfNow;
      if(!RecreateIndicators(gChartTimeframe))
      {
         // If recreation fails, skip this tick
         return;
      }
      // Reset candle reference for new timeframe
      lastCandleTime = iTime(_Symbol, gChartTimeframe, 0);
      Print("🔄 Chart timeframe changed. Reinitialized indicators for ", TimeframeStr(gChartTimeframe));
   }

   datetime currentCandleTime = iTime(_Symbol, gChartTimeframe, 0);
   double   currentPrice = SymbolInfoDouble(_Symbol, SYMBOL_BID);

   // Send price every 30 seconds (query-only, no body)
   if(now - lastPriceSentTime >= 30)
   {
      lastPriceSentTime = now;
      SendPriceUpdate(currentPrice, now);
   }

   // Detect new candle -> send compact candle snapshot in body
   if(currentCandleTime != lastCandleTime)
   {
      if(Bars(_Symbol, gChartTimeframe) >= 3)
      {
         double open   = iOpen(_Symbol, gChartTimeframe, 1);
         double close  = iClose(_Symbol, gChartTimeframe, 1);
         long   volume = (long)iVolume(_Symbol, gChartTimeframe, 1);
         string candleData = StringFormat(
               "New Candle\nSymbol: %s\nTimeframe: %s\nOpen: %.5f\nClose: %.5f\nVolume: %d\nTime: %s",
               _Symbol, TimeframeStr(gChartTimeframe), open, close, volume,
               TimeToString(currentCandleTime, TIME_DATE | TIME_MINUTES));
         SendBotData(candleData);
      }
      lastCandleTime = currentCandleTime;
   }

   // Send quantitative trend snapshot every 60 seconds
   if(now - lastTrendSentTime >= 60)
   {
      lastTrendSentTime = now;
      string trendData = BuildTrendSnapshot();
      SendBotData(trendData);
   }
}

//+------------------------------------------------------------------+
//| Build a quantitative trend snapshot                              |
//+------------------------------------------------------------------+
string BuildTrendSnapshot()
{
   if(Bars(_Symbol, gChartTimeframe) < 60)
      return "Trend Snapshot\nError: Not enough bars";

   // Prepare buffers
   double ema20[10], ema50[2], rsi[2], adx[2], atr[2];
   ArrayInitialize(ema20, 0.0);
   ArrayInitialize(ema50, 0.0);
   ArrayInitialize(rsi,   0.0);
   ArrayInitialize(adx,   0.0);
   ArrayInitialize(atr,   0.0);

   // Copy latest values (need enough history for slope)
   if(CopyBuffer(hEMA20, 0, 0, 6, ema20) < 6)  return "Trend Snapshot\nError: Not enough EMA20 data";
   if(CopyBuffer(hEMA50, 0, 0, 1, ema50) < 1)  return "Trend Snapshot\nError: Not enough EMA50 data";
   if(CopyBuffer(hRSI14, 0, 0, 1, rsi)   < 1)  return "Trend Snapshot\nError: Not enough RSI data";
   if(CopyBuffer(hADX14, 0, 0, 1, adx)   < 1)  return "Trend Snapshot\nError: Not enough ADX data";
   if(CopyBuffer(hATR14, 0, 0, 1, atr)   < 1)  return "Trend Snapshot\nError: Not enough ATR data";

   double close0  = iClose(_Symbol, gChartTimeframe, 0);
   double close5  = iClose(_Symbol, gChartTimeframe, 5);
   double close20 = iClose(_Symbol, gChartTimeframe, 20);

   // Percent changes
   double pct5  = (close5  > 0.0) ? (close0 - close5)  / close5  * 100.0 : 0.0;
   double pct20 = (close20 > 0.0) ? (close0 - close20) / close20 * 100.0 : 0.0;

   // EMA stats
   double ema20_now   = ema20[0];
   double ema20_prev5 = ema20[5];
   double ema50_now   = ema50[0];
   double ema_gap_pips = (ema20_now - ema50_now) * PipFactor();          // distance in pips
   double ema20_slope_pips_per_bar = (ema20_now - ema20_prev5) * PipFactor() / 5.0;

   // Oscillators / strength / vol
   double rsi14      = rsi[0];
   double adx14      = adx[0];            // ADX main line = buffer 0 for iADX
   double atr14_pips = atr[0] * PipFactor();

   // 20-bar high/low breakout context
   int lookback = 20;
   double hh = iHigh(_Symbol, gChartTimeframe, 1);
   double ll = iLow(_Symbol, gChartTimeframe, 1);
   for(int i=2; i<=lookback; i++)
   {
      double hi = iHigh(_Symbol, gChartTimeframe, i);
      double lo = iLow(_Symbol, gChartTimeframe, i);
      if(hi > hh) hh = hi;
      if(lo < ll) ll = lo;
   }
   bool nearHH = (hh - close0) * PipFactor() <= (atr14_pips * 0.5);
   bool nearLL = (close0 - ll) * PipFactor() <= (atr14_pips * 0.5);

   // Direction label (simple rules)
   string dir;
   if(ema20_now > ema50_now && ema20_slope_pips_per_bar > 0)      dir = "Bullish";
   else if(ema20_now < ema50_now && ema20_slope_pips_per_bar < 0) dir = "Bearish";
   else                                                           dir = "Sideways";

   // Strength label from ADX
   string strength;
   if(adx14 >= 50)           strength = "Very Strong";
   else if(adx14 >= 35)      strength = "Strong";
   else if(adx14 >= 25)      strength = "Moderate";
   else                      strength = "Weak";

   string breakout = nearHH ? "Near 20-bar High" : (nearLL ? "Near 20-bar Low" : "Neutral");

   string snapshot = StringFormat(
      "Trend Snapshot\n"
      "Symbol: %s\n"
      "Timeframe: %s\n"
      "Time: %s\n"
      "Direction: %s\n"
      "Strength(ADX14): %.1f (%s)\n"
      "EMA20: %.5f | EMA50: %.5f | Gap(pips): %.1f\n"
      "EMA20 Slope (pips/bar, 5-bar): %.2f\n"
      "Change %%: 5-bar: %.2f%% | 20-bar: %.2f%%\n"
      "RSI14: %.1f\n"
      "ATR14 (pips): %.1f\n"
      "Range Context: %s\n"
      "20-bar High: %.5f | 20-bar Low: %.5f",
      _Symbol,
      TimeframeStr(gChartTimeframe),
      TimeToString(TimeCurrent(), TIME_DATE | TIME_MINUTES),
      dir,
      adx14, strength,
      ema20_now, ema50_now, ema_gap_pips,
      ema20_slope_pips_per_bar,
      pct5, pct20,
      rsi14,
      atr14_pips,
      breakout,
      hh, ll
   );

   return snapshot;
}

//+------------------------------------------------------------------+
//| Send price update in query only (no body)                        |
//+------------------------------------------------------------------+
void SendPriceUpdate(double price, datetime now)
{
   string url = ServerHost + ":" + IntegerToString(ServerPort) +
                "/bot-data?botName=" + BotName +
                "&chartSymbol=" + _Symbol +
                "&price=" + DoubleToString(price, _Digits) +
                "&ts=" + IntegerToString((int)now); // unix ts avoids spaces

   uchar result[];
   uchar emptyBody[];                  // empty body for POST-without-body
   ArrayResize(emptyBody, 0);

   string headers = "";                // no Content-Type header

   string response_headers;
   int res = WebRequest("POST", url, headers, 5000, emptyBody, result, response_headers);

   if(res == -1)
   {
      Print("❌ Price WebRequest Error: ", GetLastError(), " | Ensure URL is allowed in MT5 settings.");
      return;
   }

   string response = CharArrayToString(result, 0, -1, CP_UTF8);
   Print("📈 Sent Price Update | ", _Symbol, " @ ", DoubleToString(price, _Digits), " | Response: ", response);
}

//+------------------------------------------------------------------+
//| Send botData in body, other params in query                      |
//+------------------------------------------------------------------+
void SendBotData(string botData)
{
   string url = ServerHost + ":" + IntegerToString(ServerPort) +
                "/bot-data?botName=" + BotName +
                "&chartSymbol=" + _Symbol;

   // UTF-8 body
   uchar data[];
   int len = StringToCharArray(botData, data, 0, WHOLE_ARRAY, CP_UTF8);
   if(len > 0 && data[len - 1] == 0)
      ArrayResize(data, len - 1);

   uchar result[];
   string headers = "Content-Type: text/plain\r\n";
   string response_headers;

   int res = WebRequest("POST", url, headers, 5000, data, result, response_headers);

   if(res == -1)
   {
      Print("❌ WebRequest Error: ", GetLastError(), " | Make sure URL is allowed in MT5 settings.");
      return;
   }

   string response = CharArrayToString(result, 0, -1, CP_UTF8);
   Print("✅ Sent: ", botData, " | Response: ", response);
}
