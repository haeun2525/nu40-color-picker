/*
 * cam_probe — 진단 전용. 어떤 명령이 "이미지/결과 이벤트"를 주는지 raw 로 본다.
 *   JPEG 디코드 안 함. 명령 보내고 1.6초간 오는 걸 모아서 구조만 찍는다.
 *   INVOKE(이미지모드) 와 SAMPLE 둘 다 시험한다.
 */
#include <Adafruit_TinyUSB.h>
#include <Wire.h>
#include "i2c_safe.h"
#include "sscma_i2c.h"

static const int LEDS[4] = { 13, 14, 15, 16 };
static void leds(int v){ for(int i=0;i<4;i++) digitalWrite(LEDS[i],v); }
static void beat(int n){ for(int i=0;i<n;i++){leds(HIGH);delay(160);leds(LOW);delay(220);} delay(700);}

#define BUF_MAX 40000
static char buf[BUF_MAX];

static void probe(const char *label, const char *cmd) {
    for (int i=0;i<8 && sscma_available()>0;i++) sscma_read(buf, BUF_MAX-1);  // 가볍게 비움
    sscma_send(cmd);

    int total=0; uint32_t t0=millis(); int reads=0, maxAvail=0;
    buf[0]=0;
    while (millis()-t0 < 1600) {                 // 고정 윈도우로 끝까지 모은다
        int a=sscma_available();
        if(a>maxAvail) maxAvail=a;
        if(a<=0){ delay(3); continue; }
        int room=BUF_MAX-1-total; if(room<=0) break;
        int n=sscma_read(buf+total, a>room?room:a);
        if(n<=0){ delay(3); continue; }
        total+=n; buf[total]=0; reads++;
    }
    if(!Serial) return;
    bool hasImg   = strstr(buf,"\"image\"")!=NULL;
    bool hasBoxes = strstr(buf,"\"boxes\"")!=NULL;
    bool hasT1    = (strstr(buf,"\"type\": 1")!=NULL)||(strstr(buf,"\"type\":1")!=NULL);
    Serial.print("["); Serial.print(label); Serial.print("] total="); Serial.print(total);
    Serial.print(" reads="); Serial.print(reads); Serial.print(" maxAvail="); Serial.print(maxAvail);
    Serial.print(" | image="); Serial.print(hasImg?"Y":"n");
    Serial.print(" boxes="); Serial.print(hasBoxes?"Y":"n");
    Serial.print(" type1="); Serial.println(hasT1?"Y":"n");
    if(hasImg){ char*p=strstr(buf,"\"image\":\""); if(p){p+=9; char*q=strchr(p,'"');
        Serial.print("   image base64 len="); Serial.println(q?(int)(q-p):-1);} }
    int head = total<220?total:220; char save=buf[head]; buf[head]=0;
    Serial.print("   head: "); Serial.println(buf); buf[head]=save;
}

void setup(){
    for(int i=0;i<4;i++) pinMode(LEDS[i],OUTPUT); leds(LOW);
    delay(1500); beat(1);
    Serial.begin(115200); delay(300);
    Wire.begin(); Wire.setClock(400000); beat(2);
    if(i2c_ping(SSCMA_ADDR)) beat(3);
    beat(4);
    if(Serial){ Serial.println(); Serial.println("== cam_probe =="); Serial.println(); }
}

void loop(){
    static uint32_t bt=0; static bool bs=true;
    if(millis()-bt>150){ bt=millis(); bs=!bs; leds(bs?HIGH:LOW);}
    probe("INVOKE 1,0,0", "AT+INVOKE=1,0,0\r\n");
    probe("INVOKE 1,0,1", "AT+INVOKE=1,0,1\r\n");
    probe("SAMPLE 1",     "AT+SAMPLE=1\r\n");
    if(Serial) Serial.println("----");
    delay(1500);
}
