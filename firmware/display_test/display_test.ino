/*
 * display_test — 원형 LCD(GC9A01)만 켜 본다.
 *
 * 목적: 배선이 맞는지, SPI가 도는지, 화면 중심이 어디인지 눈으로 확인.
 * 카메라 없이 화면 하나만 본다.
 *
 * 배선 (NU40-DK 우측 헤더에 전부 모여 있다)
 *   LCD VCC  → 3V3      (우측 27)
 *   LCD GND  → GND      (우측 24)
 *   LCD SCL  → 핀 47    (P1.15, 우측 13)   ※ SCK. 하드웨어 SPI 기본핀
 *   LCD SDA  → 핀 45    (P1.13, 우측 11)   ※ MOSI
 *   LCD CS   → 핀 44    (P1.12, 우측 10)
 *   LCD DC   → 핀 43    (P1.11, 우측 9)
 *   LCD RES  → 핀 42    (P1.10, 우측 8)
 *   LCD BLK  → 핀 41    (P1.09, 우측 7)  ※ 3V3 직결 금지 — PWM 밝기·소등을 못 한다
 */

#include <Arduino_GFX_Library.h>

// ── 핀 배정 (실측 확정값) ────────────────────────────────
#define LCD_CS  44   // P1.12
#define LCD_DC  43   // P1.11
#define LCD_RST 42   // P1.10
#define LCD_BLK 41   // P1.09  백라이트 (3V3 직결하면 밝기 조절·소등을 못 한다)
// SCK(47) / MOSI(45)는 하드웨어 SPI 기본핀이라 코드에 안 적는다.

// 버스: DC핀, CS핀, 하드웨어 SPI 사용
Arduino_DataBus *bus = new Arduino_HWSPI(LCD_DC, LCD_CS);

// 패널: GC9A01, 240x240, IPS
Arduino_GFX *gfx = new Arduino_GC9A01(bus, LCD_RST, 0 /*회전없음*/, true /*IPS*/);

void setup() {
  // ★ 백라이트부터 켠다.
  // BLK를 핀에 물려 놓고 이 두 줄이 없으면 핀이 뜬 상태라 화면이 깜깜하다.
  // 그러면 배선이 틀린 줄 알고 엉뚱한 데를 뒤지게 된다.
  pinMode(LCD_BLK, OUTPUT);
  digitalWrite(LCD_BLK, HIGH);

  Serial.begin(115200);
  // USB 시리얼이 붙을 때까지 최대 3초만 기다린다 (안 붙어도 그냥 진행)
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { }

  Serial.println(F("[1] 디스플레이 초기화 시도"));

  if (!gfx->begin(24000000)) {          // 24MHz로 시작. 불안정하면 낮춘다
    Serial.println(F("[X] gfx->begin() 실패 — 배선이나 전원 확인"));
    while (1) { delay(1000); }
  }
  Serial.println(F("[2] 초기화 성공"));

  // ── 단색 3장: 배선과 색 순서 확인 ──
  // 빨강이 파랑으로 보이면 RGB/BGR 순서가 뒤집힌 패널이다.
  gfx->fillScreen(RGB565_RED);   Serial.println(F("[3] 빨강")); delay(700);
  gfx->fillScreen(RGB565_GREEN); Serial.println(F("[4] 초록")); delay(700);
  gfx->fillScreen(RGB565_BLUE);  Serial.println(F("[5] 파랑")); delay(700);

  // ── 정렬 확인용 그림 ──
  gfx->fillScreen(RGB565_BLACK);

  // 화면 맨 가장자리 원. 이게 잘려 보이면 원형 패널의 보이는 영역이 더 작다는 뜻.
  gfx->drawCircle(120, 120, 119, RGB565_WHITE);
  gfx->drawCircle(120, 120, 100, RGB565_DARKGREY);

  // 중앙 십자. 진짜 한가운데인지 본다.
  gfx->drawFastHLine(100, 120, 40, RGB565_WHITE);
  gfx->drawFastVLine(120, 100, 40, RGB565_WHITE);

  // 위쪽 표시 — 화면이 뒤집혔는지 판단용
  gfx->fillTriangle(120, 20, 112, 36, 128, 36, RGB565_YELLOW);

  Serial.println(F("[6] 완료 — 원과 십자가 보이면 성공"));

  // ── 백라이트 밝기 확인 (브링업 문서 13번) ──
  // 배터리 수명의 핵심이라 여기서 같이 본다. 어두워졌다 다시 밝아지면 성공.
  Serial.println(F("[7] 밝기 낮춤 → 소등 → 복귀"));
  for (int v = 255; v >= 0; v -= 5) { analogWrite(LCD_BLK, v); delay(8); }
  delay(400);                       // 완전히 꺼진 상태를 눈으로 확인할 시간
  for (int v = 0; v <= 255; v += 5) { analogWrite(LCD_BLK, v); delay(8); }
  digitalWrite(LCD_BLK, HIGH);
  Serial.println(F("[8] 밝기 조절 됨"));
}

void loop() {
  // 1단계는 정지 화면만 확인한다. 아무것도 안 한다.
  delay(1000);
}
