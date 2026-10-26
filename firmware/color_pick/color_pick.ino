/*
 * color_pick — 카메라로 겨눈 곳의 "가운데 색"을 읽어 원형 LCD 에 그 색과 #RRGGBB 를 띄운다.
 *
 * 흐름: Grove Vision AI V2 에 `AT+SAMPLE=1` → 240x240 JPEG(base64)가 I2C 로 옴
 *       → base64 풀기 → JPEGDEC 로 가운데 네모 영역만 평균 → 색보정 행렬(CCM)·채도
 *       → 프레임 스무딩(EMA) → GC9A01 화면 가득 그 색 + 가운데 헥스 글자.
 *   색이 바뀔 때만 다시 그린다.
 *
 * ★ 카메라는 고정초점 30cm~∞ — 갖다 대지 말고 30cm 이상 떨어져서 겨눈다.
 *
 * ★ I2C 는 Wire 를 직접 안 쓴다. 이유는 i2c_safe.h 첫머리 참고
 *   (코어 Wire 가 타임아웃 없이 멈춘다). sscma_i2c.h 가 그 위에서 돈다.
 *
 * ★ 라이브러리: JPEGDEC (Larry Bank), GFX Library for Arduino (Arduino_GFX).
 *
 * ★ 시리얼(115200) 명령
 *     s2.0  → 채도 바꾸기 (즉시 반영, 리셋하면 기본값)
 *     u     → UF2 부트로더로 (리셋 버튼 더블탭 없이 다시 굽기)
 *
 * ★ LED 4개(온보드 P0.13~16) 깜빡임 = 진행 단계
 *     1회 setup 진입 · 2회 I2C 시작 · 3회 0x62(센서) 응답 · 4회 준비 끝
 *   (부팅 직후 시리얼은 못 볼 수 있다 — TinyUSB 가 DTR 전 출력을 버린다. LED 로 본다.)
 *
 * ★ 모든 Serial 출력은 if(Serial) 로 감싼다 — 포트가 꼬이면 Serial.print 가 통째로 멈춘다.
 *
 * 배선 (카메라 Grove 4선, 실측 검증):
 *   🟡 노랑 SCL → P0.27      ⚪️ 흰색 SDA → P0.26
 *   ⚫️ 검정 GND → GND        🔴 빨강 VCC → 3V3 (LCD 와 같이)
 *   ⚠️ 빨강을 3V3 에 꽂았으면 센서 자체 USB-C 는 꽂지 않는다 (전원 두 개 충돌).
 *   ⚠️ SenseCraft 웹 탭이 열려 있으면 센서가 I2C 를 안 본다. 닫고 할 것.
 */

#include <Adafruit_TinyUSB.h>   // 없으면 Serial 이 링크되지 않는다
#include <Wire.h>               // i2c_safe 가 핀/주변장치 설정에 Wire.begin() 을 쓴다
#include <JPEGDEC.h>            // ★ 라이브러리 매니저에서 "JPEGDEC" 설치
#include <Arduino_GFX_Library.h>// GC9A01 원형 LCD
#include "i2c_safe.h"           // 타임아웃 있는 I2C (TWIM 직접 제어)
#include "sscma_i2c.h"          // Grove Vision AI V2 패킷 송수신

/* ── 디스플레이 GC9A01 (display_test 와 동일 핀) ──────────── */
#define LCD_CS  44   // P1.12
#define LCD_DC  43   // P1.11
#define LCD_RST 42   // P1.10
#define LCD_BLK 41   // P1.09  백라이트
static Arduino_DataBus *bus = new Arduino_HWSPI(LCD_DC, LCD_CS);   // SCK47/MOSI45 기본핀
static Arduino_GFX *gfx = new Arduino_GC9A01(bus, LCD_RST, 0 /*회전없음*/, true /*IPS*/);

/* ── 색 보정 (2026-10-08 흰색 측정 RGB(113,114,105) 기준) ───────
 * WB_*  : 화이트밸런스+밝기 게인 (흰색을 255 로. 255/측정값)
 * SAT   : 채도 부스트 (1=그대로, 클수록 쨍). 탁한 카메라 보정용
 * EMA_A : 프레임 스무딩 (0~1, 작을수록 부드럽고 느림) */
/* 런타임 조절 가능 — 시리얼로 'b1.4'(밝기) 's2.5'(채도) 보내면 즉시 반영. 리플래시 불필요.
 * WB 는 흰색(113,114,105)에서 '밝기중립'으로 뽑음(색 치우침만 보정, 밝기는 안 건드림). */
static float calR = 0.98f, calG = 0.97f, calB = 1.05f;  // 화이트밸런스
static float calBright = 1.8f;                           // (CCM 쓰므로 미사용)
static float calSat = 2.0f;                              // CCM 뒤 채도 부스트 (2026-10-16 확정, 시리얼 's'로 조절)
#define EMA_A 0.40f
static inline int clampi(float v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)(v + 0.5f)); }

/* ★ 색보정 행렬 CCM (2026-10-14, 기준색 5개 최소제곱 fit, 평균오차 6.4)
 *   out = [rawR, rawG, rawB, 1] · CCM   (clamp 0~255)
 *   화이트밸런스·밝기·채도·색상 보정을 한 번에. 기준색을 다시 재서 최소제곱으로 다시 맞추면 된다. */
static const float CCM[4][3] = {
  { 3.59475f,  0.67191f,  2.53113f},
  {-2.00899f, -0.71803f, -8.04574f},
  { 3.22324f,  4.01245f, 10.52492f},
  {-352.02787f, -247.99816f, -350.35760f},
};

/* 시리얼 명령으로 밝기/채도 즉시 조절 */
static void poll_serial() {
    if (!Serial) return;
    static char cb[16]; static int ci = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (ci > 0) {
                cb[ci] = 0;
                if (cb[0] == 'u') {                 // 'u' → UF2 부트로더로 (리셋버튼 없이 재굽기)
                    Serial.println("[dfu] Entering bootloader... copy the UF2 when NRF52BOOT appears");
                    Serial.flush(); delay(80);
                    NRF_POWER->GPREGRET = 0x57;      // DFU_MAGIC_UF2_RESET (Adafruit nRF52 부트로더)
                    NVIC_SystemReset();              // 여기서 재부팅 → 아래로 안 돌아옴
                }
                float v = atof(cb + 1);
                if (cb[0] == 's') calSat = v;
                else if (cb[0] == 'b') calBright = v;
                Serial.print("[set] bright="); Serial.print(calBright);
                Serial.print(" sat="); Serial.println(calSat);
                ci = 0;
            }
        } else if (ci < 15) cb[ci++] = c;
    }
}

/* ── 센서에 보낼 명령 ───────────────────────────────────────
 * AT+SAMPLE=1 → 모델 없이 카메라 한 장(240x240 JPEG, base64 ~2.5KB)만 준다.
 *   ★ USB 직결 실측(2026-10-08): SAMPLE 이 code:0 + 완결 이미지를 확실히 준다.
 *     I2C 프로브에서도 완결 이미지를 실제로 준 건 SAMPLE(2767B)이었다.
 *   ★ 센서는 "멍청한" USB 충전기/보조배터리로 급전할 것. 맥 USB 는 DTR 로 센서를
 *     리셋시키고, NU40 3V3 는 전류가 모자라 둘 다 프레임 타임아웃(code:3)을 유발한다. */
static const char *CMD = "AT+SAMPLE=1\r\n";

/* ── 버퍼 (전역/정적 — 스택이 아니라 안전) ──────────────────
 * 이미지가 더 크면 "resp 꽉 참" 이 뜬다. 그때 이 숫자를 올리면 된다.
 * 240x240 JPEG 는 보통 base64 ~10~25KB 라 아래로 충분할 것이다. */
#define RESP_MAX 64000          // 센서 응답 JSON 전체 (base64 이미지 포함)
#define JPG_MAX  48000          // base64 를 푼 실제 JPEG 바이트
static char    resp[RESP_MAX];
static uint8_t jpg[JPG_MAX];

/* ── 중앙 평균색 누적기 (JPEGDEC 콜백이 채운다) ───────────── */
static JPEGDEC jpeg;
static int32_t sumR, sumG, sumB;
static uint32_t cnt;
static int cx, cy, half;        // 평균 낼 중앙 네모: 중심(cx,cy) ± half

/* ── 온보드 LED 로 단계 표시 ──────────────────────────────── */
static const int LEDS[4] = { 13, 14, 15, 16 };
static void leds(int v) { for (int i = 0; i < 4; i++) digitalWrite(LEDS[i], v); }
static void beat(int n) {
    for (int i = 0; i < n; i++) { leds(HIGH); delay(160); leds(LOW); delay(220); }
    delay(900);
}

/* ── base64 한 글자 → 6비트 값. 그 외(=, 개행, 공백)는 -1(건너뜀) ── */
static inline int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
/* base64 문자열 in(길이 inLen) → out 에 바이트로 푼다. 푼 길이 반환. */
static int b64_decode(const char *in, int inLen, uint8_t *out, int outMax) {
    int outLen = 0, bits = 0, buf = 0;
    for (int i = 0; i < inLen; i++) {
        int v = b64val(in[i]);
        if (v < 0) continue;                 // 패딩/개행 등은 무시
        buf = (buf << 6) | v; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (outLen < outMax) out[outLen++] = (uint8_t)((buf >> bits) & 0xFF);
            else break;                      // 버퍼 넘침 — 여기서 멈춘다
        }
    }
    return outLen;
}

/* ── JPEGDEC 콜백: 블록 하나가 풀릴 때마다 불린다 ─────────────
 * 화면 전체를 그릴 필요가 없다. 중앙 네모에 드는 픽셀만 평균에 더한다.
 * 픽셀 형식은 RGB565(16bit, 리틀엔디안) — 8bit 로 늘려서 더한다. */
static int drawCB(JPEGDRAW *p) {
    for (int row = 0; row < p->iHeight; row++) {
        int py = p->y + row;
        if (py < cy - half || py > cy + half) continue;
        uint16_t *line = p->pPixels + row * p->iWidth;
        for (int col = 0; col < p->iWidth; col++) {
            int px = p->x + col;
            if (px < cx - half || px > cx + half) continue;
            uint16_t c = line[col];
            int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
            sumR += (r << 3) | (r >> 2);     // 5bit → 8bit
            sumG += (g << 2) | (g >> 4);     // 6bit → 8bit
            sumB += (b << 3) | (b >> 2);     // 5bit → 8bit
            cnt++;
        }
    }
    return 1;   // 계속
}

/* 응답에서 "image" 값(base64)의 시작/끝 포인터를 찾는다. 공백 허용: `"image": "..."`.
 * 끝 따옴표까지 다 왔으면 true. (전송이 덜 왔으면 end=NULL, false) */
static bool find_image(char *buf, char **start, char **end) {
    char *k = strstr(buf, "\"image\"");
    if (!k) { *start = *end = NULL; return false; }
    char *s = strchr(k + 7, '"');          // 값 여는 따옴표 (콜론·공백 건너뜀)
    if (!s) { *start = *end = NULL; return false; }
    s++;
    char *e = strchr(s, '"');              // 값 닫는 따옴표 (base64 안엔 " 없음)
    *start = s; *end = e;
    return e != NULL;
}

/* 센서에 이미지 한 장 요청해서 jpg[] 에 담는다. JPEG 바이트 수 반환(0=실패).
 * ★ 단발 INVOKE 는 카메라가 식어 ack(361B)만 올 때가 많다. 프로브처럼
 *   이미지가 올 때까지 몇 번 재시도해 카메라를 깨운다. (2026-10-08 실측 근거) */
static int capture_jpeg() {
    char *p, *q;
    for (int attempt = 0; attempt < 4; attempt++) {
        /* 1) 남은 응답 비우기 */
        for (int i = 0; i < 8 && sscma_available() > 0; i++) sscma_read(resp, RESP_MAX - 1);

        /* 2) ★ 카메라 깨우기 — INVOKE 로 모델 한 번 돌려 카메라를 스트리밍 상태로.
         *    (SAMPLE 을 차갑게 부르면 프레임 타임아웃 code:3 이 난다) 응답은 버린다. */
        sscma_send("AT+INVOKE=1,0,1\r\n");
        { uint32_t w = millis();
          while (millis() - w < 400) { int a = sscma_available();
              if (a > 0) sscma_read(resp, RESP_MAX - 1); else delay(5); } }

        /* 3) 남은 것 비우고 SAMPLE 로 한 장 */
        for (int i = 0; i < 8 && sscma_available() > 0; i++) sscma_read(resp, RESP_MAX - 1);
        sscma_send(CMD);

        int total = 0;
        uint32_t t0 = millis();
        resp[0] = 0;
        while (millis() - t0 < 2500) {
            int avail = sscma_available();
            if (avail <= 0) { delay(3); continue; }
            int room = RESP_MAX - 1 - total;
            if (room <= 0) break;
            int n = sscma_read(resp + total, avail > room ? room : avail);
            if (n <= 0) { delay(3); continue; }
            total += n; resp[total] = 0;
            if (find_image(resp, &p, &q)) break;
        }

        if (find_image(resp, &p, &q)) {
            int b64len = (int)(q - p);
            int jpgLen = b64_decode(p, b64len, jpg, JPG_MAX);
            if (Serial) { Serial.print("got "); Serial.print(total);
                          Serial.print("B / base64 "); Serial.print(b64len);
                          Serial.print(" / jpeg "); Serial.print(jpgLen);
                          Serial.print("B (try "); Serial.print(attempt + 1); Serial.println(")"); }
            return jpgLen;
        }

        /* 진단: 실패한 시도의 응답 앞부분(code 값 등 보려고) */
        if (Serial) { Serial.print("[try "); Serial.print(attempt + 1);
            Serial.print(" failed] total="); Serial.print(total); Serial.print(" raw: ");
            int h = total < 150 ? total : 150; char s = resp[h]; resp[h] = 0;
            Serial.println(resp); resp[h] = s; }
    }
    return 0;
}

/* 측정색을 화면 가득 칠하고 가운데 헥스+RGB 를 쓴다. */
static void draw_color(int R, int G, int B) {
    gfx->fillScreen(gfx->color565(R, G, B));
    int lum = (R * 299 + G * 587 + B * 114) / 1000;          // 밝기
    uint16_t txt = (lum > 140) ? RGB565_BLACK : RGB565_WHITE; // 대비색
    char hex[8];  snprintf(hex, sizeof(hex), "#%02X%02X%02X", R, G, B);
    char rgb[20]; snprintf(rgb, sizeof(rgb), "%d,%d,%d", R, G, B);
    gfx->setTextColor(txt);
    gfx->setTextSize(3);                                      // 글자 18x24, 7자=126px
    gfx->setCursor(120 - (int)strlen(hex) * 18 / 2, 92);
    gfx->print(hex);
    gfx->setTextSize(2);                                      // 글자 12x16
    gfx->setCursor(120 - (int)strlen(rgb) * 12 / 2, 128);
    gfx->print(rgb);
}

void setup() {
    for (int i = 0; i < 4; i++) pinMode(LEDS[i], OUTPUT);
    leds(LOW);
    delay(1500);
    beat(1);                                   // setup 진입

    Serial.begin(115200);
    delay(300);

    Wire.begin();                              // i2c_safe 가 쓸 핀/주변장치 설정
    Wire.setClock(400000);                     // 이미지가 커서 빠른 버스가 유리.
                                               // ⚠️ 읽기가 불안하면 100000 으로 되돌릴 것.
    beat(2);                                   // I2C 시작

    if (i2c_ping(SSCMA_ADDR)) beat(3);         // 0x62 응답하면 3번

    /* 디스플레이 켜기 (display_test 와 동일) */
    pinMode(LCD_BLK, OUTPUT);
    digitalWrite(LCD_BLK, HIGH);
    gfx->begin();
    bus->beginWrite(); bus->writeC8D8(0x36, 0x08); bus->endWrite();  // ★ 색순서: 이 빌드는 BGR(0x08)이 맞음(빨강 댔을 때 파랗게 나와서 확인)
    gfx->fillScreen(RGB565_BLACK);
    gfx->setTextColor(RGB565_WHITE); gfx->setTextSize(2);
    gfx->setCursor(96, 112); gfx->print("...");

    beat(4);                                   // 준비 끝
    if (Serial) {
        Serial.println();
        Serial.println("== color pick ==  camera center color -> #RRGGBB");
        Serial.println("Needs the JPEGDEC library. Sensor red wire = 3V3. Close any SenseCraft browser tab.");
        Serial.println();
    }
}

void loop() {
    poll_serial();                             // 시리얼로 밝기/채도 즉시 조절

    /* loop 가 살아있다는 표시 */
    static uint32_t bt = 0; static bool bs = true;
    if (millis() - bt > 150) { bt = millis(); bs = !bs; leds(bs ? HIGH : LOW); }

    int jpgLen = capture_jpeg();
    if (jpgLen > 0 && jpeg.openRAM(jpg, jpgLen, drawCB)) {
        jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
        int W = jpeg.getWidth(), H = jpeg.getHeight();
        cx = W / 2; cy = H / 2;
        half = (W < H ? W : H) / 10; if (half < 4) half = 4;   // 중앙 ~20% 네모
        sumR = sumG = sumB = 0; cnt = 0;
        jpeg.decode(0, 0, 0);
        jpeg.close();

        if (cnt > 0) {
            int rawR = sumR / (int)cnt, rawG = sumG / (int)cnt, rawB = sumB / (int)cnt;

            /* ── 보정: 색보정 행렬(CCM) → 프레임 스무딩 ── */
            float fr = rawR*CCM[0][0] + rawG*CCM[1][0] + rawB*CCM[2][0] + CCM[3][0];
            float fg = rawR*CCM[0][1] + rawG*CCM[1][1] + rawB*CCM[2][1] + CCM[3][1];
            float fb = rawR*CCM[0][2] + rawG*CCM[1][2] + rawB*CCM[2][2] + CCM[3][2];
            /* CCM 뒤 채도 부스트 — 쨍하게 (시리얼 's1.6' 등으로 실시간 조절) */
            float L = 0.30f*fr + 0.59f*fg + 0.11f*fb;
            fr = L + (fr - L) * calSat; fg = L + (fg - L) * calSat; fb = L + (fb - L) * calSat;
            static float eR = -1, eG = -1, eB = -1;
            if (eR < 0) { eR = fr; eG = fg; eB = fb; }
            else { eR += (fr - eR) * EMA_A; eG += (fg - eG) * EMA_A; eB += (fb - eB) * EMA_A; }
            int R = clampi(eR), G = clampi(eG), B = clampi(eB);

            /* 화면 갱신 — 색이 눈에 띄게 바뀔 때만(깜빡임 줄임) */
            static int lastR = -99, lastG = -99, lastB = -99;
            if (abs(R - lastR) + abs(G - lastG) + abs(B - lastB) > 6) {
                draw_color(R, G, B);
                lastR = R; lastG = G; lastB = B;
            }

            if (Serial) {
                char hex[8]; snprintf(hex, sizeof(hex), "#%02X%02X%02X", R, G, B);
                Serial.print("  raw("); Serial.print(rawR); Serial.print(","); Serial.print(rawG);
                Serial.print(","); Serial.print(rawB); Serial.print(") -> corrected RGB(");
                Serial.print(R); Serial.print(","); Serial.print(G); Serial.print(","); Serial.print(B);
                Serial.print(")  "); Serial.println(hex);
            }
        } else if (Serial) {
            Serial.println("  Decoded, but the center pixels are 0 (check scale/coordinates).");
        }
    } else if (jpgLen > 0 && Serial) {
        Serial.println("  JPEG decode failed (data damaged or too large).");
    }

    delay(1200);   // 이미지 전송이 느리므로 천천히 반복
}
