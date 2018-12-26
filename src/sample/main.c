// Last Update:2018-12-25 20:44:48
/**
 * @file main.c
 * @brief 
 * @author felix
 * @version 0.1.00
 * @date 2018-12-11
 */

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "dbg.h"
#include "dev_core.h"
#include "rtmp_wapper.h"
#include "sig_ctl.h"
#include "mqtt.h"
#include "control.h"

typedef struct {
    char *pSignal;
    int nSignal;
} MqttSignal;

typedef struct {
    char *pUrl;
    int nTimeout;
    int nInputAudioType;
    int nOutputAudioType;
    int nTimePolic;
    char *pClientId;
    MqttQoS nQos;
    char *pUserName;
    char *pPasswd;
    char *pTopic;
    char *pHost;
    int nPort;
    int nStreamSts;
    MqttContex *pMqttContex;
    RtmpPubContext *pContext;
    pthread_mutex_t mutex;
    CoreDevice *pDev;
} app_t;

#define ITEM_LIST \
    ADD_SIGNAL_ITEM( pushLiveStart ) \
    ADD_SIGNAL_ITEM( pushLiveStop ) \
    ADD_SIGNAL_ITEM( pushSucceed )

#define ADD_SIGNAL_ITEM( item ) item,

enum {
   ITEM_LIST 
};

#undef ADD_SIGNAL_ITEM
#define ADD_SIGNAL_ITEM( item ) { #item, item },
#define ARRSZ(arr) (sizeof(arr)/sizeof(arr[0]))

static MqttSignal gSignalList[] = 
{
    ITEM_LIST
};

enum {
    STREAM_STATUS_RUNNING,
    STREAM_STATUS_STOPED,
};

static app_t app = 
{
    .pUrl = "rtmp://pili-publish.caster.test.cloudvdn.com/caster-test/test18",
    .nTimeout = 10,
    .nInputAudioType = RTMP_PUB_AUDIO_AAC, 
    .nOutputAudioType = RTMP_PUB_AUDIO_AAC,
    .nTimePolic = RTMP_PUB_TIMESTAMP_ABSOLUTE,
    .pClientId = "ipc-rtmp-mqtt-10-2",
    .nQos = 2,
    .pUserName = NULL,
    .pPasswd = NULL,
    .pTopic = "pushLive",
    .pHost = "emqx.qnservice.com",
    .nPort = 1883,
};

int GetMqttSignal( char *pMqttSignal )
{
    int i = 0;

    for ( i=0; i<ARRSZ(gSignalList); i++ ) {
        if ( strncmp( gSignalList[i].pSignal, pMqttSignal,
                      strlen(gSignalList[i].pSignal) ) == 0 ) {
            return gSignalList[i].nSignal;
        }
    }

    return -1;
}

int VideoFrameCallBack ( char *_pFrame, 
                   int _nLen, int _nIskey, double _dTimeStamp, 
                   unsigned long _nFrameIndex, unsigned long _nKeyFrameIndex, 
                   int streamno )
{
    int ret = 0;

    pthread_mutex_lock( &app.mutex );
    ret = RtmpSendVideo( app.pContext, _pFrame, _nLen, _nIskey, (unsigned int) _dTimeStamp );
    if ( ret < 0 ) {
        LOGE("RtmpSendVideo error\n");
    }
    pthread_mutex_unlock( &app.mutex );

    return 0;
}

int AudioFrameCallBack( char *_pFrame, int _nLen, double _dTimeStamp,
                     unsigned long _nFrameIndex, int streamno )
{
    int ret = 0;

    pthread_mutex_lock( &app.mutex );
    ret = RtmpSendAudio( app.pContext, _pFrame, _nLen, (unsigned int) _dTimeStamp );
    if ( ret < 0 ) {
        LOGE("RtmpSendAudio error\n");
    }
    pthread_mutex_unlock( &app.mutex );
    return 0;
}

void EventLoop()
{
    char message[1000] = { 0 };
    unsigned int nIOCtrlType = 0;
    int nSize = 1000, nSignal = 0;
    int ret = 0;
    char *resp = "pushSucceed";

    ret = LinkRecvIOCtrl( app.pMqttContex->nSession, &nIOCtrlType, message, &nSize, 6000 );
    if ( ret == MQTT_SUCCESS ) {
        LOGI("message = %s\n", message );
        nSignal = GetMqttSignal( message );
        switch( nSignal ) {
        case pushLiveStart:
            if ( app.pDev  && (app.nStreamSts == STREAM_STATUS_STOPED) ) {

                LOGI("get signal pushLiveStart, start to push rtmp stream\n");
                app.pDev->startStream( STREAM_MAIN );
                ret = LinkSendIOResponse( app.pMqttContex->nSession, 0, resp, strlen(resp) );
                LOGI("ret = %d\n", ret );
                LOGI("set app stream running\n");
                app.nStreamSts = STREAM_STATUS_RUNNING;
            }
            break;
        case pushLiveStop:
            if ( app.pDev ) {
                LOGI("get signal pushLiveStop, stop to push rtmp stream\n");
                app.pDev->stopStream();
                app.nStreamSts = STREAM_STATUS_STOPED;
            }
            break;
        case pushSucceed:
            LOGI("pushSucceed\n");
            break;
        default:
            break;
        }

    } else if ( ret != MQTT_RETRY  && ret != MQTT_ERR_INVAL ){
        LOGI("ret = %d\n", ret );
    }
}

int main()
{
    int ret = 0;

    app.pDev = NewCoreDevice();
    app.nStreamSts = STREAM_STATUS_STOPED;
    if ( !app.pDev ) {
        LOGE("NewCoreDevice() error\n");
        return 0;
    }

    LOGI("init mqtt\n");
    app.pMqttContex = MqttNewContex( app.pClientId, app.nQos, app.pUserName, app.pPasswd,
                                     app.pTopic, app.pHost, app.nPort, NULL ) ;
    if ( !app.pMqttContex ) {
        LOGE("MqttNewContex error\n");
        return 0;
    }

    LOGI("init rtmp lib\n");
    pthread_mutex_init( &app.mutex, NULL );
    app.pContext = RtmpNewContext( app.pUrl, app.nTimeout,
                                   app.nInputAudioType, app.nOutputAudioType, app.nTimePolic );
    if ( !app.pContext ) {
        LOGE("RtmpNewContext() error\n");
        return 0;
    }

    ret = RtmpConnect( app.pContext );
    if ( ret < 0 ) {
        LOGE("RtmpConnect error\n");
        return 0;
    }

    LOGI("start stream\n");
    app.pDev->init( AUDIO_AAC, 0, VideoFrameCallBack, AudioFrameCallBack );

    for (;;) {
        char memUsed[16] = { 0 };

        EventLoop();

        DbgGetMemUsed( memUsed );
        LOGI("memory used : %s\n", memUsed );
    }

    return 0;
}
