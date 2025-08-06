//+------------------------------------------------------------------+
//|                                                  MarketAstra.mq5 |
//|     Send candle & price data to Swift backend                    |
//+------------------------------------------------------------------+
#property strict

input string ServerHost = "http://127.0.0.1";        // Host
input int    ServerPort = 3456;                      // Port
input string BotName    = "Market Astro";            // Bot name
input ENUM_TIMEFRAMES CandleTimeframe = PERIOD_M1;   // Candle timeframe

datetime lastCandleTime = 0;
double   lastPrice = 0;

//+------------------------------------------------------------------+
//| Expert initialization                                            |
//+------------------------------------------------------------------+
int OnInit()
  {
   Print("MarketAstra EA initialized ✅");
   lastCandleTime = iTime(_Symbol, CandleTimeframe, 0);
   lastPrice      = iClose(_Symbol, CandleTimeframe, 0);
   return(INIT_SUCCEEDED);
  }

//+------------------------------------------------------------------+
//| OnTick handler                                                   |
//+------------------------------------------------------------------+
void OnTick()
  {
   datetime currentCandle = iTime(_Symbol, CandleTimeframe, 0);
   double   currentPrice  = iClose(_Symbol, CandleTimeframe, 0);

   // Detect new candle
   if(currentCandle != lastCandleTime)
     {
      lastCandleTime = currentCandle;
      SendBotData("New Candle: " + DoubleToString(currentPrice, _Digits));
     }

   // Detect price change
   if(currentPrice != lastPrice)
     {
      lastPrice = currentPrice;
      SendBotData("Price Change: " + DoubleToString(currentPrice, _Digits));
     }
  }

//+------------------------------------------------------------------+
//| Send botData in body, other params in query                      |
//+------------------------------------------------------------------+
void SendBotData(string botData)
  {
   string url = ServerHost + ":" + IntegerToString(ServerPort) +
                "/bot-data?botName=" + BotName +
                "&chartSymbol=" + _Symbol;

   // Prepare request body as raw text
   uchar data[];
   int len = StringToCharArray(botData, data, 0, WHOLE_ARRAY, CP_UTF8);
   if(len > 0 && data[len - 1] == 0) // trim null terminator
      ArrayResize(data, len - 1);

   uchar result[];
   string headers = "Content-Type: text/plain\r\n";
   string response_headers;

   int res = WebRequest("POST",
                        url,
                        headers,
                        5000,
                        data,
                        result,
                        response_headers);

   if(res == -1)
     {
      Print("❌ WebRequest Error: ", GetLastError(), " | Make sure URL is allowed in MT5 settings.");
      return;
     }

   string response = CharArrayToString(result, 0, -1, CP_UTF8);
   Print("✅ Sent: ", botData, " | Response: ", response);
  }
