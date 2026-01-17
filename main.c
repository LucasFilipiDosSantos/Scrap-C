#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

#define URI_TARGET      "https://www.imdb.com/pt/calendar/?region=BR"
#define URI_BASE        "https://www.imdb.com"
#define URI_WS_ECHO     "wss://echo.websocket.org"
#define OUT_FILE        "filmes_formatado.txt"

#define CSS_ITEM        "ipc-metadata-list-summary-item__tc"
#define CSS_TITLE       "ipc-metadata-list-summary-item__t"
#define CSS_META        "ipc-metadata-list-summary-item__li"

#define WAIT_SEC        5
#define BUF_SZ          4096

typedef struct {
  char   *pData;
  size_t  stSize;
} MEM_CHUNK, *PMEM_CHUNK;

static size_t stWriteMem(void *pPtr, size_t stSz, size_t stNmemb, void *pStream)
{
  size_t stRealSz = stSz * stNmemb;
  PMEM_CHUNK pstMem = (PMEM_CHUNK)pStream;
  char *pNew;

  pNew = realloc(pstMem->pData, pstMem->stSize + stRealSz + 1);
  if (!pNew) return 0;

  pstMem->pData = pNew;
  memcpy(&(pstMem->pData[pstMem->stSize]), pPtr, stRealSz);
  pstMem->stSize += stRealSz;
  pstMem->pData[pstMem->stSize] = 0;

  return stRealSz;
}

void vParseNode(const char *kpszRaw, FILE *fpDest, int iIdx)
{
  const char *pszPos, *pszTmp, *pszEnd;
  char szLabel[512] = {0};
  char szHref[512]  = {0};
  char szInfo[BUF_SZ] = {0};
  long lDelta;


  pszPos = strstr(kpszRaw, CSS_TITLE);
  if (pszPos) {
    while (pszPos > kpszRaw && *pszPos != '<') pszPos--;
    

    pszTmp = strstr(pszPos, "href=\"");
    if (pszTmp) {
      pszTmp += 6;
      pszEnd = strchr(pszTmp, '"');
      if (pszEnd) {
        lDelta = pszEnd - pszTmp;
        if (lDelta < 511) {
          strncpy(szHref, pszTmp, lDelta);
          szHref[lDelta] = '\0';
        }
      }
    }

    pszTmp = strchr(pszPos, '>');
    if (pszTmp) {
      pszTmp++;
      pszEnd = strstr(pszTmp, "</a>");
      if (pszEnd) {
        lDelta = pszEnd - pszTmp;
        if (lDelta < 511) {
          strncpy(szLabel, pszTmp, lDelta);
          szLabel[lDelta] = '\0';
        }
      }
    }
  }

  pszPos = kpszRaw;
  while ((pszPos = strstr(pszPos, CSS_META)) != NULL) {
    pszTmp = strchr(pszPos, '>');
    if (pszTmp) {
      pszTmp++;
      pszEnd = strchr(pszTmp, '<');
      if (pszEnd) {
        lDelta = pszEnd - pszTmp;
        if (lDelta > 0 && lDelta < 100) {
          if (strlen(szInfo) + lDelta + 3 < BUF_SZ) {
             if (strlen(szInfo) > 0) strcat(szInfo, ", ");
             strncat(szInfo, pszTmp, lDelta);
          }
        }
      }
    }
    pszPos++;
  }

  if (strlen(szLabel) > 0) {
    fprintf(fpDest, "ITEM #%02d\n", iIdx);
    fprintf(fpDest, "TITLE: %s\n", szLabel);
    fprintf(fpDest, "LINK : %s%s\n", URI_BASE, szHref);
    fprintf(fpDest, "INFO : %s\n", szInfo);
    fprintf(fpDest, "--------------------------------------------------\n");
  }
}

void vProcessHtml(const char *kpszBody) 
{
  FILE *fp;
  const char *pszCur, *pszEnd, *pszStart, *pszScan;
  char szTag[1024];
  char *pszChunk;
  int iDepth, iCt = 0;
  long lLen, lChunkSz;

  if (!kpszBody || !strstr(kpszBody, CSS_ITEM)) return;

  fp = fopen(OUT_FILE, "w");
  if (!fp) return;

  pszCur = kpszBody;
  while ((pszCur = strstr(pszCur, "<div")) != NULL) {
    pszEnd = strchr(pszCur, '>');
    if (pszEnd) {
      lLen = pszEnd - pszCur;
      if (lLen > 0 && lLen < 1023) {
        strncpy(szTag, pszCur, lLen);
        szTag[lLen] = '\0';

        if (strstr(szTag, CSS_ITEM)) {
          pszStart = pszEnd + 1;
          iDepth = 1;
          pszScan = pszStart;
          lChunkSz = 0;

          while (*pszScan != '\0') {
            if (strncmp(pszScan, "<div", 4) == 0) iDepth++;
            else if (strncmp(pszScan, "</div>", 6) == 0) iDepth--;

            if (iDepth == 0) {
              lChunkSz = pszScan - pszStart;
              break; 
            }
            pszScan++;
          }

          if (lChunkSz > 0) {
            iCt++;
            pszChunk = malloc(lChunkSz + 1);
            if (pszChunk) {
              strncpy(pszChunk, pszStart, lChunkSz);
              pszChunk[lChunkSz] = '\0';
              vParseNode(pszChunk, fp, iCt);
              free(pszChunk);
            }
          }
        }
      }
    }
    pszCur++;
  }
  fclose(fp);
  fprintf(stdout, "SYS: %d items formatted to %s\n", iCt, OUT_FILE);
}

int iFetchData(void) 
{
  CURL *pstCurl;
  CURLcode iRes;
  MEM_CHUNK stBuf;
  struct curl_slist *pstHead = NULL;
  long lHttpCode;

  pstCurl = curl_easy_init();
  if (!pstCurl) return 1;

  stBuf.pData = malloc(1);
  stBuf.stSize = 0;

  pstHead = curl_slist_append(pstHead, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
  pstHead = curl_slist_append(pstHead, "Connection: keep-alive");

  curl_easy_setopt(pstCurl, CURLOPT_URL, URI_TARGET);
  curl_easy_setopt(pstCurl, CURLOPT_HTTPHEADER, pstHead);
  curl_easy_setopt(pstCurl, CURLOPT_WRITEFUNCTION, stWriteMem);
  curl_easy_setopt(pstCurl, CURLOPT_WRITEDATA, (void *)&stBuf);
  curl_easy_setopt(pstCurl, CURLOPT_FOLLOWLOCATION, 1L); 
  curl_easy_setopt(pstCurl, CURLOPT_SSL_VERIFYPEER, 0L);

  iRes = curl_easy_perform(pstCurl);

  if (iRes == CURLE_OK) {
    curl_easy_getinfo(pstCurl, CURLINFO_RESPONSE_CODE, &lHttpCode);
    if (lHttpCode == 200) {
      vProcessHtml(stBuf.pData);
    } else {
      fprintf(stderr, "ERR: HTTP %ld\n", lHttpCode);
    }
  }

  curl_slist_free_all(pstHead);
  free(stBuf.pData);
  curl_easy_cleanup(pstCurl);
  
  return (iRes == CURLE_OK && lHttpCode == 200) ? 0 : 1;
}

int main(void) 
{
  curl_global_init(CURL_GLOBAL_ALL);
  fprintf(stdout, "SYS: Starting Extraction (PID: %d)...\n", getpid());
  
  if (iFetchData() != 0) {
    fprintf(stderr, "SYS: Failed.\n");
  } else {
    fprintf(stdout, "SYS: Done.\n");
  }

  curl_global_cleanup();
  return 0;
}