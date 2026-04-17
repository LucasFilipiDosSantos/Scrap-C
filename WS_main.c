#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

#define WS_ENDPOINT     "wss://stream.binance.com:9443/ws/btcusdt@trade"
#define RECV_BUF_SZ     4096
#define MAX_RETRY       50
#define RETRY_DELAY     5
#define FLG_WHALE       10000.0

void vParseTrade(const char *kpszPayload);
int  iRunWorker(void);

void vParseTrade(const char *kpszPayload)
{
  const char *pszPos;
  char *pszEnd;
  double dPx   = 0.0;
  double dQty  = 0.0;
  double dNotional;

  pszPos = strstr(kpszPayload, "\"p\":\"");
  if (pszPos) dPx = strtod(pszPos + 5, &pszEnd);

  pszPos = strstr(kpszPayload, "\"q\":\"");
  if (pszPos) dQty = strtod(pszPos + 5, &pszEnd);

  if (dPx > 0 && dQty > 0) {
    dNotional = dPx * dQty;

    fprintf(stdout, "BTC: $%.2f | Vol: %.4f BTC | Total: $%.2f %s\n", 
            dPx, dQty, dNotional, 
            (dNotional >= FLG_WHALE) ? "<< WHALE ALERT" : "");
  }
}

int iRunWorker(void)
{
  CURL *pstCurl;
  CURLcode iRes;
  size_t stRead;
  char szBuf[RECV_BUF_SZ];
  const struct curl_ws_frame *pstMeta;

  pstCurl = curl_easy_init();
  if (!pstCurl) return -1;

  curl_easy_setopt(pstCurl, CURLOPT_URL, WS_ENDPOINT);
  curl_easy_setopt(pstCurl, CURLOPT_CONNECT_ONLY, 2L);
  curl_easy_setopt(pstCurl, CURLOPT_SSL_VERIFYPEER, 0L);

  iRes = curl_easy_perform(pstCurl);
  if (iRes != CURLE_OK) {
    fprintf(stderr, "ERR: conn_fail [%d]\n", iRes);
    curl_easy_cleanup(pstCurl);
    return 1;
  }

  while (1) {
    iRes = curl_ws_recv(pstCurl, szBuf, sizeof(szBuf) - 1, &stRead, &pstMeta);
    
    if (iRes != CURLE_OK) {
      if (iRes == CURLE_AGAIN) continue;
      break;
    }

    if (stRead > 0) {
      szBuf[stRead] = 0;
      
      if (pstMeta->flags & CURLWS_TEXT) {
        if (szBuf[0] == '{') vParseTrade(szBuf);
      }
      
      if (pstMeta->flags & CURLWS_CLOSE) break;
    }
  }

  curl_easy_cleanup(pstCurl);
  return 0;
}

int main(void)
{
  int iAtmpt = 0;

  curl_global_init(CURL_GLOBAL_ALL);

  while (iAtmpt < MAX_RETRY) {
    if (iRunWorker() != 0) {
      iAtmpt++;
      fprintf(stderr, "SYS: retry %d/%d in %ds\n", iAtmpt, MAX_RETRY, RETRY_DELAY);
      sleep(RETRY_DELAY);
    } else {
      iAtmpt = 0;
    }
  }

  curl_global_cleanup();
  return 0;
}