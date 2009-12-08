/* 
** Copyright 2006, The Android Open Source Project
** Copyright (c) 2009, Code Aurora Forum. All rights reserved.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
#define LOG_TAG "CND_PROCESS"

#include <cutils/sockets.h>
#include <cutils/jstring.h>
#include <cutils/record_stream.h>
#include <utils/Log.h>
#include <utils/SystemClock.h>
#include <pthread.h>
//#include <utils/Parcel.h>
#include <binder/Parcel.h>
#include <cutils/jstring.h>

#include <sys/types.h>
#include <pwd.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <alloca.h>
#include <sys/un.h>
#include <assert.h>
#include <netinet/in.h>
#include <cutils/properties.h>
#include <dirent.h>
#include <cnd_event.h>
#include <cnd.h>	
#include <cne_svc.h>




namespace android {

#define SOCKET_NAME_CND "cnd"

// match with constant in .java
#define MAX_COMMAND_BYTES (8 * 1024)

// Basically: memset buffers that the client library
// shouldn't be using anymore in an attempt to find
// memory usage issues sooner.
#define MEMSET_FREED 1

#define NUM_ELEMS(a)     (sizeof (a) / sizeof (a)[0])

/* Constants for response types */
#define SOLICITED_RESPONSE 0
#define UNSOLICITED_MESSAGE 1

typedef struct {
    int commandNumber;
    void (*dispatchFunction) (Parcel &p, struct RequestInfo *pRI);
    int(*responseFunction) (Parcel &p, void *response, size_t responselen);
} CommandInfo;

typedef struct {
    int messageNumber;
    int (*responseFunction) (Parcel &p, void *response, size_t responselen);
} UnsolMessageInfo;

typedef struct RequestInfo {
    int32_t token;      //this is not CND_Token 
    int fd;
    CommandInfo *pCI;
    struct RequestInfo *p_next;
    char cancelled;
    char local;         // responses to local commands do not go back to command process
} RequestInfo;


/*******************************************************************/

static int s_registerCalled = 0;

static pthread_t s_tid_dispatch;

static int s_started = 0;

static int s_fdListen = -1;
static int s_fdCommand = -1;

static int cnmSvcFd = -1;

static struct cnd_event s_commands_event[MAX_FD_EVENTS];
static struct cnd_event s_listen_event;
static int command_index = 0;

static pthread_mutex_t s_pendingRequestsMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_writeMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_startupMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_startupCond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t s_dispatchMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_dispatchCond = PTHREAD_COND_INITIALIZER;

static RequestInfo *s_pendingRequests = NULL;

static RequestInfo *s_toDispatchHead = NULL;
static RequestInfo *s_toDispatchTail = NULL;


/*******************************************************************/
static void dispatchVoid (Parcel& p, RequestInfo *pRI);
static void dispatchString (Parcel& p, RequestInfo *pRI);
static void dispatchStrings (Parcel& p, RequestInfo *pRI);
static void dispatchInts (Parcel& p, RequestInfo *pRI);
static void dispatchWlanInfo(Parcel &p, RequestInfo *pRI);
static void dispatchWlanScanResults(Parcel &p, RequestInfo *pRI);
static void dispatchRaw(Parcel& p, RequestInfo *pRI);
static int responseInts(Parcel &p, void *response, size_t responselen);
static int responseStrings(Parcel &p, void *response, size_t responselen);
static int responseString(Parcel &p, void *response, size_t responselen);
static int responseVoid(Parcel &p, void *response, size_t responselen);
static int responseRaw(Parcel &p, void *response, size_t responselen);
static int responseStartTrans(Parcel &p, void *response, size_t responselen);
static int sendResponseRaw (const void *data, size_t dataSize, int fdCommand);
static int sendResponse (Parcel &p, int fd);
static int eventRatChange (Parcel &p, void *response, size_t responselen);
static char *strdupReadString(Parcel &p);
static void writeStringToParcel(Parcel &p, const char *s);
static void memsetString (char *s);
static int writeData(int fd, const void *buffer, size_t len);
static void unsolicitedMessage(int unsolMessage, void *data, size_t datalen, int fd);
static void processCommand (int command, void *data, size_t datalen, CND_Token t);
static int processCommandBuffer(void *buffer, size_t buflen, int fd);
static void invalidCommandBlock (RequestInfo *pRI);
static void onCommandsSocketClosed(void);
static void processCommandsCallback(int fd, void *param);
static void listenCallback (int fd, void *param);
static void *eventLoop(void *param);
static int checkAndDequeueRequestInfo(struct RequestInfo *pRI);
static void cnd_commandComplete(CND_Token t, CND_Errno e, void *response, size_t responselen);

extern "C" const char * requestToString(int request);
extern "C" void cne_processCommand (int command, void *data, size_t datalen);
extern "C" void cne_regMessageCb(cne_messageCbType cbFn);

/** Index == commandNumber */
static CommandInfo s_commands[] = {
#include "cnd_commands.h"
};

static UnsolMessageInfo s_unsolMessages[] = {
#include "cnd_unsol_messages.h"
};

#define TEMP_BUFFER_SIZE		(80)


void cnd_sendUnsolicitedMsg(int targetFd, int msgType, int dataLen, void *data)
{
  int fd;	
  
  if (targetFd == 0)   // TODO find the correct fd, who keeps track of it? 
	fd = cnmSvcFd;
  else
	fd = targetFd; 

  LOGD ("cnd_sendUnsolicitedMsg: Fd=%d, msgType=%d, datalen=%d\n", 
        targetFd, msgType, dataLen);  

  unsolicitedMessage(msgType, data, dataLen, fd);


}

static void
processCommand (int command, void *data, size_t datalen, CND_Token t)
{

  LOGD ("processCommand: command=%d, datalen=%d", command, datalen);    

  /* Special handling for iproute2 command to setup iproute2 table */
  if (command == CNE_REQUEST_CONFIG_IPROUTE2_CMD) 
  {
    char **pStrings;
    *pStrings = (char *)data;
    LOGD ("processCommand: str1=%s, str2=%s", pStrings[0], pStrings[1]);    
    // Call iproute2 API
        
  }
  cne_processCommand(command, data, datalen);
  cnd_commandComplete(t, CND_E_SUCCESS, NULL, 0);
}

static char *
strdupReadString(Parcel &p)
{
    size_t stringlen;
    const char16_t *s16;
            
    s16 = p.readString16Inplace(&stringlen);
    
    return strndup16to8(s16, stringlen);
}

static void writeStringToParcel(Parcel &p, const char *s)
{
    char16_t *s16;
    size_t s16_len;
    s16 = strdup8to16(s, &s16_len);
    p.writeString16(s16, s16_len);
    free(s16);
}


static void
memsetString (char *s)
{
    if (s != NULL) {
        memset (s, 0, strlen(s));
    }
}


static void
invalidCommandBlock (RequestInfo *pRI)
{
    //LOGE("invalid command block for token %d request %s", 
    //            pRI->token, requestToString(pRI->pCI->commandNumber));
}

/** Callee expects NULL */
static void 
dispatchVoid (Parcel& p, RequestInfo *pRI)
{

    processCommand(pRI->pCI->commandNumber, NULL, 0, pRI);
}

/** Callee expects const char * */
static void
dispatchString (Parcel& p, RequestInfo *pRI)
{
    status_t status;
    size_t datalen;
    size_t stringlen;
    char *string8 = NULL;

    string8 = strdupReadString(p);

    processCommand(pRI->pCI->commandNumber, string8,
                       sizeof(char *), pRI);

#ifdef MEMSET_FREED
    memsetString(string8);
#endif

    free(string8);
    return;
}

/** Callee expects const char ** */
static void
dispatchStrings (Parcel &p, RequestInfo *pRI)
{
    int32_t countStrings;
    status_t status;
    size_t datalen;
    char **pStrings;

    status = p.readInt32 (&countStrings);

    if (status != NO_ERROR) {
        goto invalid;
    }


    if (countStrings == 0) {
        // just some non-null pointer
        pStrings = (char **)alloca(sizeof(char *));
        datalen = 0;
    } else if (((int)countStrings) == -1) {
        pStrings = NULL;
        datalen = 0;
    } else {
        datalen = sizeof(char *) * countStrings;
    
        pStrings = (char **)alloca(datalen);

        for (int i = 0 ; i < countStrings ; i++) {
            pStrings[i] = strdupReadString(p);
           
        }
    }

    processCommand(pRI->pCI->commandNumber, pStrings, datalen, pRI);

    if (pStrings != NULL) {
        for (int i = 0 ; i < countStrings ; i++) {
#ifdef MEMSET_FREED
            memsetString (pStrings[i]);
#endif
            free(pStrings[i]);
        }

#ifdef MEMSET_FREED
        memset(pStrings, 0, datalen);
#endif
    }
    
    return;
invalid:
    invalidCommandBlock(pRI);
    return;
}

/** Callee expects const int * */
static void
dispatchInts (Parcel &p, RequestInfo *pRI)
{
    int32_t count;
    status_t status;
    size_t datalen;
    int *pInts;

    status = p.readInt32 (&count);
    
    LOGD ("dispatchInts: status=%d, count=%d", status, count);

    if (status != NO_ERROR || count == 0) {
        goto invalid;
    }

    datalen = sizeof(int) * count;
    pInts = (int *)alloca(datalen);

  
    for (int i = 0 ; i < count ; i++) {
        int32_t t;

        status = p.readInt32(&t);
        pInts[i] = (int)t;
     

        if (status != NO_ERROR) {
            goto invalid;
        }
   }

   processCommand(pRI->pCI->commandNumber, const_cast<int *>(pInts), 
                       datalen, pRI);

#ifdef MEMSET_FREED
    memset(pInts, 0, datalen);
#endif

    return;
invalid:
    invalidCommandBlock(pRI);
    return;
}


static void
dispatchWlanInfo(Parcel &p, RequestInfo *pRI)
{
    int32_t t;
    status_t status;
   CneWlanInfoType args;

    memset(&args, 0, sizeof(args));

    status = p.readInt32 (&t);
    args.status = (int)t;
    status = p.readInt32 (&t);
    args.rssi = (int)t;
    args.ssid = strdupReadString(p);
    
    LOGD ("dispatchWlanInfo: status=%ld, rssi=%ld, ssid=%s",
          args.status, args.rssi, args.ssid);
    
 
    processCommand(pRI->pCI->commandNumber, &args, sizeof(args), pRI);

    return;
}

static void
dispatchWlanScanResults(Parcel &p, RequestInfo *pRI)
{
    int32_t t;
    status_t status;
    CneWlanScanResultsType args;
    int32_t numItems;

    status = p.readInt32 (&t);
    //args = (CneWlanScanResultsType *)malloc(sizeof(CneWlanScanResultsType)*numItems);
    args.numItems = (int)t;
    int max = (t < CNE_MAX_SCANLIST_SIZE)? t:CNE_MAX_SCANLIST_SIZE;

    for (int i = 0; i < max; i++) 
    {
        //args->numItems = numItems;
        status = p.readInt32 (&t);
        args.scanList[i].level = (int)t;
        status = p.readInt32 (&t);
        args.scanList[i].frequency = (int)t;
        args.scanList[i].ssid = strdupReadString(p);
        args.scanList[i].bssid = strdupReadString(p);
        args.scanList[i].capabilities = strdupReadString(p);

        LOGD ("dispatchWlanScanResults: max=%d, level=%ld, freq=%ld, ssid=%s, bssid=%s, cap=%s", 
              args.numItems, args.scanList[i].level, args.scanList[i].frequency, 
              args.scanList[i].ssid, args.scanList[i].bssid, args.scanList[i].capabilities);

    }

 
    processCommand(pRI->pCI->commandNumber, &args, sizeof(args), pRI);

    return;
}

static void 
dispatchRaw(Parcel &p, RequestInfo *pRI)
{
    int32_t len;
    status_t status;
    const void *data;

    status = p.readInt32(&len);

    if (status != NO_ERROR) {
        goto invalid;
    }

    // The java code writes -1 for null arrays
    if (((int)len) == -1) {
        data = NULL;
        len = 0;
    } 

    data = p.readInplace(len);

    processCommand(pRI->pCI->commandNumber, const_cast<void *>(data), len, pRI);

    return;
invalid:
    invalidCommandBlock(pRI);
    return;
}

static int
writeData(int fd, const void *buffer, size_t len)
{
    size_t writeOffset = 0; 
    const uint8_t *toWrite;

    toWrite = (const uint8_t *)buffer;

    LOGD ("writeData: len=%d",len);
    while (writeOffset < len) {
        ssize_t written;
        do {
            written = write (fd, toWrite + writeOffset,
                                len - writeOffset);
        } while (written < 0 && errno == EINTR);

        if (written >= 0) {
            writeOffset += written;
        } else {   // written < 0
            LOGE ("writeData: unexpected error on write errno:%d", errno);
            close(fd);
            return -1;
        }
    }

    return 0;
}

static int
sendResponseRaw (const void *data, size_t dataSize, int fdCommand)
{
    int fd = fdCommand;
    int ret;
    uint32_t header;


    LOGD ("sendResponseRaw: fdCommand=%d", fdCommand);
    if (fdCommand < 0) {
        return -1;
    }

    if (dataSize > MAX_COMMAND_BYTES) {
        LOGE("sendResponseRaw: packet larger than %u (%u)",
                MAX_COMMAND_BYTES, (unsigned int )dataSize);

        return -1;
    }
    
    pthread_mutex_lock(&s_writeMutex);

    header = htonl(dataSize);

    ret = writeData(fd, (void *)&header, sizeof(header));

    if (ret < 0) {
        return ret;
    }

    writeData(fd, data, dataSize);

    if (ret < 0) {
      pthread_mutex_unlock(&s_writeMutex);
      return ret;
    }

    pthread_mutex_unlock(&s_writeMutex);

    return 0;
}

static int
sendResponse (Parcel &p, int fd)
{
   
    return sendResponseRaw(p.data(), p.dataSize(), fd);
}

static int 
responseStartTrans(Parcel &p, void *response, size_t responselen)
{
    int numInts;


    LOGD ("responseStartTrans: len=%d",responselen);

    if (response == NULL && responselen != 0) {
        LOGE("invalid response: NULL");
        return CND_E_INVALID_RESPONSE;
    }
  

    int *p_int = (int *) response;
    //bool tmp = p_int[1];
    char *p_char = (char *)response;

    p.writeInt32(p_int[0]);
    //writeStringToParcel(p, (const char *)p_char[4]);


    //p.write(&tmp, 1);

    LOGD ("responseStartTrans: int=%d, bool=%d",p_int[0], p_char[4]);   

    return 0;
}

/** response is an int* pointing to an array of ints*/
 
static int 
responseInts(Parcel &p, void *response, size_t responselen)
{
    int numInts;


    LOGD ("responseInts: len=%d",responselen);

    if (response == NULL && responselen != 0) {
        LOGE("invalid response: NULL");
        return CND_E_INVALID_RESPONSE;
    }
    if (responselen % sizeof(int) != 0) {
        LOGE("invalid response length %d expected multiple of %d\n", 
            (int)responselen, (int)sizeof(int));
        return CND_E_INVALID_RESPONSE;
    }

    int *p_int = (int *) response;

    numInts = responselen / sizeof(int *);
    p.writeInt32 (numInts);

    /* each int*/
  
    for (int i = 0 ; i < numInts ; i++) {
      
        p.writeInt32(p_int[i]);
    }



    return 0;
}

/** response is a char **, pointing to an array of char *'s */
static int responseStrings(Parcel &p, void *response, size_t responselen)
{
    int numStrings;
    
    if (response == NULL && responselen != 0) {
        LOGE("invalid response: NULL");
        return CND_E_INVALID_RESPONSE;
    }
    if (responselen % sizeof(char *) != 0) {
        LOGE("invalid response length %d expected multiple of %d\n", 
            (int)responselen, (int)sizeof(char *));
        return CND_E_INVALID_RESPONSE;
    }

    if (response == NULL) {
        p.writeInt32 (0);
    } else {
        char **p_cur = (char **) response;

        numStrings = responselen / sizeof(char *);
        p.writeInt32 (numStrings);

        /* each string*/
     
        for (int i = 0 ; i < numStrings ; i++) {
    
            writeStringToParcel (p, p_cur[i]);
        }
  
     
    }
    return 0;
}


/**
 * NULL strings are accepted 
 * FIXME currently ignores responselen
 */
static int responseString(Parcel &p, void *response, size_t responselen)
{

    LOGD ("responseString called"); 
   /* one string only */
    writeStringToParcel(p, (const char *)response);

    return 0;
}

static int responseVoid(Parcel &p, void *response, size_t responselen)
{
    return 0;
}

static int responseRaw(Parcel &p, void *response, size_t responselen)
{
    if (response == NULL && responselen != 0) {
        LOGE("invalid response: NULL with responselen != 0");
        return CND_E_INVALID_RESPONSE;
    }

    // The java code reads -1 size as null byte array
    if (response == NULL) {
        p.writeInt32(-1);       
    } else {
        p.writeInt32(responselen);
        p.write(response, responselen);
    }

    return 0;
}

static int eventRatChange(Parcel &p, void *response, size_t responselen)
{
    if (response == NULL && responselen != 0) 
    {
        LOGE("invalid response: NULL");
        return CND_E_INVALID_RESPONSE;
    }

    CneRatInfoType *p_cur = ((CneRatInfoType *) response);
    p.writeInt32((int)p_cur->rat);

   /* if ((p_cur->rat == CNE_RAT_WLAN_HOME) ||
        (p_cur->rat == CNE_RAT_WLAN_ENTERPRISE) ||
        (p_cur->rat == CNE_RAT_WLAN_OPERATOR) ||
        (p_cur->rat == CNE_RAT_WLAN_OTHER) ||
        (p_cur->rat == CNE_RAT_WLAN_ANY))
    {
      writeStringToParcel (p, p_cur->wlan.ssid);
    }
  */
    if (p_cur->rat == CNE_RAT_WLAN)
    {
      writeStringToParcel (p, p_cur->wlan.ssid);
    }
    return 0;
}

static int
checkAndDequeueRequestInfo(struct RequestInfo *pRI)
{
    int ret = 0;
    
    if (pRI == NULL) {
        return 0;
    }

    pthread_mutex_lock(&s_pendingRequestsMutex);

    for(RequestInfo **ppCur = &s_pendingRequests 
        ; *ppCur != NULL 
        ; ppCur = &((*ppCur)->p_next)
    ) {
        if (pRI == *ppCur) {
            ret = 1;

            *ppCur = (*ppCur)->p_next;
            break;
        }
    }

    pthread_mutex_unlock(&s_pendingRequestsMutex);

    return ret;
}

static void onCommandsSocketClosed()
{
    int ret;
    RequestInfo *p_cur;

    /* mark pending requests as "cancelled" so we dont report responses */

    ret = pthread_mutex_lock(&s_pendingRequestsMutex);
    assert (ret == 0);

    p_cur = s_pendingRequests;

    for (p_cur = s_pendingRequests 
            ; p_cur != NULL
            ; p_cur  = p_cur->p_next
    ) {
        p_cur->cancelled = 1;
    }

    ret = pthread_mutex_unlock(&s_pendingRequestsMutex);
    assert (ret == 0);
}

static void unsolicitedMessage(int unsolMessage, void *data, size_t datalen, int fd)
{
    int unsolMessageIndex;
    int ret;

    if (s_registerCalled == 0) {
        // Ignore RIL_onUnsolicitedResponse before cnd_int
        LOGW("unsolicitedMessage called before cnd_init");
        return;
    }
      
    Parcel p;

    p.writeInt32 (UNSOLICITED_MESSAGE);
    p.writeInt32 (unsolMessage);

    ret = s_unsolMessages[unsolMessage]
                .responseFunction(p, data, datalen);
  
    if (ret != 0) {
        // Problem with the response. Don't continue;
        LOGE("unsolicitedMessage: problem with response");
	return;
    }

    LOGD ("unsolicitedMessage: sending Response");
    ret = sendResponse(p, fd);
    
    return;

}

static int
processCommandBuffer(void *buffer, size_t buflen, int fd)
{
    Parcel p;
    status_t status;
    int32_t request;
    int32_t token;
    RequestInfo *pRI;
    int ret;

    p.setData((uint8_t *) buffer, buflen);

    // status checked at end
    status = p.readInt32(&request);
    status = p.readInt32 (&token);

    LOGD ("processCommandBuffer: request=%d, token=%d, fd=%d", request, token, fd);
    if (status != NO_ERROR) {
        LOGE("invalid request block");
        return 0;
    }

    if (request < 1 || request >= (int32_t)NUM_ELEMS(s_commands)) {
        LOGE("unsupported request code %d token %d", request, token);
        // TBD: this should return a response
        return 0;
    }


    pRI = (RequestInfo *)calloc(1, sizeof(RequestInfo));

    pRI->token = token;
    pRI->fd = fd;
    pRI->pCI = &(s_commands[request]);

    ret = pthread_mutex_lock(&s_pendingRequestsMutex);
    assert (ret == 0);

    pRI->p_next = s_pendingRequests;
    s_pendingRequests = pRI;

    ret = pthread_mutex_unlock(&s_pendingRequestsMutex);
    assert (ret == 0);

    pRI->pCI->dispatchFunction(p, pRI);    

    return 0;
}

static void processCommandsCallback(int fd, void *param)
{
    RecordStream *p_rs;
    void *p_record;
    size_t recordlen;
    int ret;

    LOGD ("processCommandsCallback: fd=%d, s_fdCommand=%d", fd, s_fdCommand);
  
    p_rs = (RecordStream *)param;

   
    for (;;) {
        /* loop until EAGAIN/EINTR, end of stream, or other error */
        ret = record_stream_get_next(p_rs, &p_record, &recordlen);

        LOGD ("processCommandsCallback: len=%d, ret=%d", recordlen, ret);
        if (ret == 0 && p_record == NULL) {
	   LOGD ("processCommandsCallback: end of stream");
            /* end-of-stream */
            break;
        } else if (ret < 0) {
            break;
        } else if (ret == 0) { /* && p_record != NULL */
            processCommandBuffer(p_record, recordlen, fd);
	   
        }
    }

    //LOGD ("processCommandsCallback: errno=%d, ret=%d", errno, ret);
    if (ret == 0 || !(errno == EAGAIN || errno == EINTR)) {
        /* fatal error or end-of-stream */
        if (ret != 0) {
            LOGE("error on reading command socket errno:%d\n", errno);
        } else {
            LOGW("EOS.  Closing command socket.");
        }
        
        LOGD ("processCommandsCallback: Closing");
        close(s_fdCommand);
        s_fdCommand = -1;

        // cnd_event_del(&s_commands_event); // TODO - need to clean up properly

	command_index = 0;

        record_stream_free(p_rs);

        /* start listening for new connections again */
        cnd_event_add(&s_listen_event);

        onCommandsSocketClosed();
    }

}

static void listenCallback (int fd, void *param)
{
    int ret;
    int err;
    int is_cnm_svc_socket;
    RecordStream *p_rs;
    int i;
    char tmpBuf[10];
    int32_t pid, pid2, pid3, pid4;

    struct sockaddr_un peeraddr;
    socklen_t socklen = sizeof (peeraddr);

    struct ucred creds;
    socklen_t szCreds = sizeof(creds);
  

    struct passwd *pwd = NULL;

    assert (s_fdCommand < 0);
    assert (fd == s_fdListen);
    
    LOGD ("listenCallback: called");
 
     
    s_fdCommand = accept(s_fdListen, (sockaddr *) &peeraddr, &socklen);

 
    if (s_fdCommand < 0 ) {
      LOGE("Error on accept() errno:%d", errno);
      /* start listening for new connections again */
      cnd_event_add(&s_listen_event);
	  return;
    }

    errno = 0;
      
    err = getsockopt(s_fdCommand, SOL_SOCKET, SO_PEERCRED, &creds, &szCreds);

 	cnmSvcFd = s_fdCommand; // save command descriptor to be used for communication

    ret = fcntl(s_fdCommand, F_SETFL, O_NONBLOCK);

    if (ret < 0) {
      LOGE ("Error setting O_NONBLOCK errno = %d", errno);
    }

    LOGI("listenCallback: accept new connection, fd=%d", s_fdCommand);
    
    p_rs = record_stream_new(s_fdCommand, MAX_COMMAND_BYTES);
    
 
    // note: persistent = 1, not removed from table
    if (command_index >= MAX_FD_EVENTS)
    {
      LOGE ("Error: exceeding number of supported connection");
	  return;
    }
    cnd_event_set (&s_commands_event[command_index], s_fdCommand, 1, processCommandsCallback, p_rs);

    cnd_event_add (&s_commands_event[command_index]);
 
    command_index++;
   
    return;

}


static void *
eventLoop(void *param)
{
    int ret;
    int filedes[2];

    LOGD ("eventLoop: s_started=%d", s_started);

    pthread_mutex_lock(&s_startupMutex);

    s_started = 1;
    pthread_cond_broadcast(&s_startupCond);

    pthread_mutex_unlock(&s_startupMutex);
   
    cnd_event_loop();
     

    return NULL;
}

extern "C" void 
cnd_startEventLoop(void)
{
    int ret;
    pthread_attr_t attr;
    
    /* spin up eventLoop thread and wait for it to get started */
    s_started = 0;
    pthread_mutex_lock(&s_startupMutex);

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);    
    ret = pthread_create(&s_tid_dispatch, &attr, eventLoop, NULL);

 
    while (s_started == 0) {
        pthread_cond_wait(&s_startupCond, &s_startupMutex);
    }
    
 
    pthread_mutex_unlock(&s_startupMutex);

    if (ret < 0) {
        LOGE("Failed to create dispatch thread errno:%d", errno);
        return;
    }
}

extern "C" void 
cnd_init (void)
{
    int ret;

    if (s_registerCalled > 0) {
        LOGE("cnd_init has been called more than once. "
                "Subsequent call ignored");
        return;
    }
   
    s_registerCalled = 1;

    cnd_event_init();	

    cne_regMessageCb(cnd_sendUnsolicitedMsg);


    s_fdListen = android_get_control_socket(SOCKET_NAME_CND);
    if (s_fdListen < 0) {
        LOGE("Failed to get socket '" SOCKET_NAME_CND "'");
        exit(-1);
    }

    ret = listen(s_fdListen, 4);

    if (ret < 0) {
        LOGE("Failed to listen on control socket '%d': %s",
             s_fdListen, strerror(errno));
        exit(-1);
    }


    LOGD ("cnd_init: adding listenCallback event, fd=%d",s_fdListen);

    /* note: non-persistent to accept only one connection at a time */
    //cnd_event_set (&s_listen_event, s_fdListen, 0, listenCallback, NULL);
    
    // persistent to accept multiple connections at same time
    cnd_event_set (&s_listen_event, s_fdListen, 1, listenCallback, NULL);

    cnd_event_add (&s_listen_event);

   
}


//extern "C" void - TBD -may want to change this function to extern "C" and 
// be called from CneCet where Cne components (SRM/SPM/CDE) may send the
// response to Cne java
static void
cnd_commandComplete(CND_Token t, CND_Errno e, void *response, size_t responselen)
{
    RequestInfo *pRI;
    int ret;
    size_t errorOffset;

    pRI = (RequestInfo *)t;

    LOGD ("cnd_commandComplete: started");

    if (!checkAndDequeueRequestInfo(pRI)) {
        LOGE ("cnd_commandComplete: invalid CND_Token");
        return;
    }

    if (pRI->local > 0) {
         goto done;
    }

   
    if (pRI->cancelled == 0) {
        Parcel p;

        p.writeInt32 (SOLICITED_RESPONSE);
        p.writeInt32 (pRI->token);
        errorOffset = p.dataPosition();

        p.writeInt32 (e);


        if (e == CND_E_SUCCESS) {
            /* process response on success */
            ret = pRI->pCI->responseFunction(p, response, responselen);
            LOGD ("cnd_commandComplete: ret = %d", ret);
            /* if an error occurred, rewind and mark it */
            if (ret != 0) {
                p.setDataPosition(errorOffset);
                p.writeInt32 (ret);
            }
        } else {
            LOGE ("cnd_commandComplete: Error");
        }

        if (pRI->fd < 0) {
            LOGE ("cnd_commandComplete: Command channel closed");
        }
        LOGD ("cnd_commandComplete: sending Response");
        sendResponse(p, pRI->fd);
    }

done:
    free(pRI);
}

} /* namespace android */


