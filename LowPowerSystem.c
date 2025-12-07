#include "cmsis_os2.h"
#include "e_shell_list.h"
#include "gpio_if.h"
#include "adc_if.h"
#include "stdbool.h"
#include "mqtt_api.h"
#include "cJSON.h"

#define v1 40
#define v2 41
#define v3 103
#define h1 66
#define h2 102
#define h3 39

char Client_Id[] = "your Cloud Client_Id";
char User_name[] = "your Cloud User_name";
char SecretKey[] = "your Cloud SecretKey";
int LowPowerMode = 0;
int NetState = 0;
char CloudIp[] = "x.x.x.x";
int Light = 0;
bool LowPowerModeStateChanged = false;
bool ScreenOn = true;
bool ScreenBlinking = false;

static osTimerId_t timer = NULL;
static void TimerCb()
{
    ScreenOn = !ScreenOn;
}
static void TimerInit()
{
    timer = osTimerNew(TimerCb, osTimerPeriodic, NULL, NULL);
}

static void TimerEntry()
{
    if(LowPowerMode == 0)
    {
        if( timer != NULL)
        {
            osTimerStop(timer);
            ScreenBlinking = false;
            ScreenOn = true;
        }
    }
    else if(LowPowerMode  == 5)
    {
        osTimerStart(timer, 5000);
        ScreenOn = true;
        ScreenBlinking = true;
    }
    else if(LowPowerMode == 10)
    {
        osTimerStart(timer, 10000);
        ScreenBlinking = true;
        ScreenOn = true;
    }
}
static bool Ispressed(uint32_t gpio, int statue)
{
    uint16_t now_statue;
    bool ret = false;
    GpioRead(gpio, &now_statue);
    if(now_statue == statue)
    {
        osDelay(10);
        GpioRead(gpio, &now_statue);
        if(now_statue == statue)
        {
            ret = true;
            while(now_statue  == statue)
            {
                GpioRead(gpio, &now_statue);
                osDelay(10);
            }
        }
    }
    return ret;
}

static int MatrixScan()
{
    for(int i=1; i<4;i++)
    {
        GpioWrite(v1, 0);
        GpioWrite(v2, 0);
        GpioWrite(v3, 0);
        switch(i)
        {
            case 1:GpioWrite(v1, 1);break;
            case 2:GpioWrite(v2, 1);break;
            case 3:GpioWrite(v3, 1);break;
        }
        if(Ispressed(h1,1))return i;
        if(Ispressed(h2,1))return i+3;
        if(Ispressed(h3,1))return i+6;
    }
    return 0;
}

static void MatrixEntry()
{
    int MatrixId = MatrixScan(); 
    if(MatrixId > 0)
    { 
        switch(MatrixId)
        {
            case 1: LowPowerMode = 0;break;
            case 5: LowPowerMode = 5;break;
            case 9: LowPowerMode = 10;break;
        }
        LowPowerModeStateChanged = true;
    }
    
}

static void Oled_Init()
{
    OledInit();
    OledClear();
    ScreenOn = true;
    ScreenBlinking = false;    
}

static void Oled_Display()
{
    OledShowString(2,0,"NetState:");
    OledShowString(2,2,"CloudIp:");
    OledShowString(2,4,"Light:");
    OledShowString(2,6,"LowPowerMode:");

    OledShowNum(8*10, 0,NetState,1,16);
    OledShowString(8*9, 2,CloudIp);
    OledShowNum(8*8, 4,Light,4,16);
    OledShowNum(8*13, 6,LowPowerMode,3,16);
    osDelay(100);
}

static void OledEntry()
{
    if(ScreenOn &&(ScreenBlinking == false))
    {
        Oled_Display();
    }
    else if(ScreenBlinking &&(ScreenOn == false))
    {
        OledClear();
    }
    else if(ScreenBlinking &&(ScreenOn == true))
    {
        Oled_Display();
    }
}

#define Adc_id 0
#define Adc_channel 5
static DevHandle dev_light = NULL;
static void Adc_Init()
{
    dev_light = AdcOpen(Adc_id);
}

static void AdcEntry()
{
    AdcRead(dev_light, Adc_channel, &Light);
}

static MQTT_CLI_Handle *mqtt = NULL;

static void mqtt_cb(MessageData* md)
{
    MQTTMessage *json_str =(char*)md -> message -> payload;
    cJSON *root = cJSON_Parse(json_str);
    if(root == NULL)
    {
        printf("[JSON] error\n");
        return ;
    }
    cJSON *value = cJSON_GetObjectItem(root, "data");
    int value_int = atoi(value->valuestring);
    switch(value_int)
    {
        case 0: LowPowerMode = 0;LowPowerModeStateChanged = true;break;
        case 5: LowPowerMode = 5;LowPowerModeStateChanged = true;break;
        case 10: LowPowerMode = 10;LowPowerModeStateChanged = true;break;
    }
    printf("[Rxcv]%s STATUE:%d \n", json_str, value_int);
    cJSON_Delete(root);
}

static void mqtt_Init()
{
    mqtt = MqttApiInit();
    if(mqtt == NULL)
    return ;
    MQTT_CONNE_T conn = {0};
    sprintf(conn.addr, "mqtt.nlecloud.com");
    conn.port = 1883;
    sprintf(conn.clinetId, Client_Id);
    sprintf(conn.userName, User_name);
    sprintf(conn.passwd, SecretKey);
    conn.willFlag = 0;
    if(MqttApiConnect(mqtt, &conn,5000) != MQTT_STATUS_SUCCESS)
    {
        printf("[Mqtt] failed Connect!\n");
        NetState = 0;
        MqttApiDeinit(mqtt);
        mqtt = NULL;
        return ;
    }
    printf("[Mqtt] success Connect!\n");
    NetState = 1;
    return ;
}

static void mqtt_publish()
{
    MQTT_PUB_T pub = {0};
    sprintf(pub.pub_topic, "/sys/%s/%s/sensor/datas", User_name, Client_Id);
    sprintf(pub.data, "{\"datatype\":1,\"datas\":{\"light\":%d}}",Light);
    pub.len = strlen(pub.data);
    pub.qos = QOS0;
    pub.retained = 0;
    if(MqttApiPublish(mqtt,&pub) != MQTT_STATUS_SUCCESS)
    {
        printf("[Mqtt] failed Publish!\n");
        MqttApiDeinit(mqtt);
        mqtt = NULL;
        return ;
    }
    printf("[Mqtt] success Publish!\n");
    return ;
}



static int mqtt_subscribe()
{
    static MQTT_SUB_T sub = {0};
    sprintf(sub.sub_topic,"/sys/%s/%s/sensor/cmdreq",User_name, Client_Id);
    sub.qos = QOS0;
    sub.cb = mqtt_cb;
    if(MqttApiSubscribe(mqtt, &sub) != MQTT_STATUS_SUCCESS)
    {
        printf("Mqtt subscribe failed!\n");
        return 0;
    }
    printf("Mqtt subscribe success\n");
    return 1; 
}
static void ThreadOled()
{
    Oled_Init();
    TimerInit();
    while(1)
    {
        OledEntry();
        MatrixEntry();
        if(LowPowerModeStateChanged == true)
        {
            TimerEntry();
            LowPowerModeStateChanged = false;
        }
    }
}

static void ThreadAdc()
{
    Adc_Init();
    while(1)
    {
        AdcEntry();
        osDelay(800);
    }
}
static void ThreadMqtt()
{
    osDelay(3000);
    mqtt_Init();
    int Sub_state = mqtt_subscribe();    
    while (1)
    {
        if(Sub_state == 0)
        {
            Sub_state = mqtt_subscribe();
        }
        if(NetState == 0 || mqtt == NULL)
        {
            mqtt_Init();
        }
        else 
        {
            mqtt_publish();
        }
        osDelay(5000);
    }
}
static void ThreadEntry()
{
    osThreadAttr_t attr = {0};
    attr.stack_size = 4096;
    attr.priority = osPriorityNormal;
    
    attr.name = "ThreadTask";
    osThreadNew(ThreadOled, NULL, &attr);

    attr.name = "ThreadAdc";
    osThreadNew(ThreadAdc, NULL, &attr);

    attr.name = "ThreadMqtt";
    osThreadNew(ThreadMqtt, NULL, &attr);
}

APP_FEATURE_INIT(ThreadEntry);
