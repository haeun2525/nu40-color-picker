/* i2c_safe.h — 타임아웃이 있는 I2C. 헤더 온리.
 *
 * ★★ 왜 Wire 를 안 쓰는가 (2026-09-02, 이 프로젝트에서 제일 오래 헤맨 문제)
 *
 *   코어의 Wire 드라이버(libraries/Wire/Wire_nRF52.cpp)는 하드웨어 이벤트를
 *   **타임아웃 없는 무한 루프**로 기다린다:
 *
 *       while(!_p_twim->EVENTS_RXSTARTED && !_p_twim->EVENTS_ERROR);
 *       while(!_p_twim->EVENTS_STOPPED);      // ← 에러 탈출조차 없다
 *       while(!_p_twim->EVENTS_SUSPENDED);    // ← 여기도
 *
 *   슬레이브가 버스를 붙잡으면(클럭 스트레칭, 글리치, 전원 순간 강하)
 *   **호출이 영원히 안 돌아온다.** 그러면 loop 가 멈추고 화면이 통째로 얼어붙는다.
 *
 *   ⚠️ 이게 핵심이다 — **재시도·실패 카운터·워치독 카운터가 전부 무의미하다.**
 *     실패를 반환하는 게 아니라 돌아오지 않기 때문이다. 그 위에서 뭘 해도 못 구한다.
 *     실제로 "실패 30번이면 IMU 끄기"를 넣었는데 아무 소용이 없었다.
 *
 *   → 그래서 TWIM 레지스터를 직접 다루되 **모든 대기에 상한**을 둔다.
 *     Wire 는 begin() 으로 핀 설정과 주변장치 활성화만 시키고, 전송은 여기서 한다.
 *
 * ★ 버스가 물리면 되살린다 (i2c_recover)
 *   슬레이브가 SDA 를 잡고 있을 때 SCL 을 9번 흔들어 주면 대개 놓는다.
 *   I2C 표준 복구 절차다. 이걸 안 하면 한 번 물린 버스는 전원을 껐다 켜야 풀린다.
 *
 * ⚠️ EasyDMA 는 **RAM 만** 읽는다. 문자열 상수를 그대로 넘기면 안 된다 (플래시에 있다).
 *   그래서 보내기 전에 RAM 버퍼로 복사한다.
 * ⚠️ TWIM 의 MAXCNT 는 8비트다. 한 번에 255바이트를 넘길 수 없다.
 */
#ifndef I2C_SAFE_H
#define I2C_SAFE_H

#include <Arduino.h>
#include <Wire.h>

/* 이벤트 하나를 이만큼 기다리고 포기한다.
 * 100kHz 에서 64바이트는 약 6ms 다. 여유를 두되, 멈춤이 눈에 띄지 않을 만큼 짧게. */
#define I2C_EVENT_TIMEOUT_US 12000
#define I2C_BUF_MAX 64

/* Wire 가 쓰는 것과 같은 인스턴스·핀이어야 한다 (Wire_nRF52.cpp 의 전역 정의 참고) */
#define I2C_TWIM NRF_TWIM0

static uint8_t  i2c_tx[I2C_BUF_MAX];
static uint8_t  i2c_rx[I2C_BUF_MAX];
/* 타임아웃이 몇 번 났는지. 0 이 아니면 배선·전원·버스 속도를 의심할 것. */
static uint16_t i2c_timeouts = 0;

/* 이벤트를 기다리되 상한을 둔다. true = 이벤트 도착, false = 시간 초과 */
static inline bool i2c_wait(volatile uint32_t *evt, volatile uint32_t *err) {
    uint32_t t0 = micros();
    while (!*evt) {
        if (err && *err) return false;
        if ((uint32_t)(micros() - t0) > I2C_EVENT_TIMEOUT_US) { i2c_timeouts++; return false; }
    }
    return true;
}

/* ★ 물린 버스 되살리기. SCL 을 9번 흔들어 슬레이브가 SDA 를 놓게 한다.
 * TWIM 을 잠깐 꺼야 핀을 GPIO 로 쓸 수 있다. */
static inline void i2c_recover() {
    I2C_TWIM->ENABLE = 0;

    pinMode(PIN_WIRE_SCL, OUTPUT);
    pinMode(PIN_WIRE_SDA, INPUT_PULLUP);
    for (int i = 0; i < 9; i++) {
        digitalWrite(PIN_WIRE_SCL, LOW);  delayMicroseconds(5);
        digitalWrite(PIN_WIRE_SCL, HIGH); delayMicroseconds(5);
    }
    /* STOP 조건을 흉내낸다 — SCL 이 높은 동안 SDA 를 낮췄다 올린다 */
    pinMode(PIN_WIRE_SDA, OUTPUT);
    digitalWrite(PIN_WIRE_SDA, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_WIRE_SCL, HIGH); delayMicroseconds(5);
    digitalWrite(PIN_WIRE_SDA, HIGH); delayMicroseconds(5);

    /* 핀을 다시 TWIM 에 넘긴다. Wire.begin() 이 하는 것과 같은 설정이다. */
    Wire.begin();
}

/* 쓰기. stop=false 면 repeated start 를 위해 버스를 잡아둔다.
 * 반환값: 보낸 바이트 수. 0 이면 실패(타임아웃 포함). */
static inline int i2c_write(uint8_t addr, const uint8_t *data, int n, bool stop) {
    if (n <= 0 || n > I2C_BUF_MAX) return 0;
    for (int i = 0; i < n; i++) i2c_tx[i] = data[i];   /* ⚠️ EasyDMA 는 RAM 만 읽는다 */

    I2C_TWIM->ADDRESS = addr;
    I2C_TWIM->EVENTS_ERROR = 0;
    I2C_TWIM->EVENTS_STOPPED = 0;
    I2C_TWIM->EVENTS_SUSPENDED = 0;
    I2C_TWIM->EVENTS_TXSTARTED = 0;
    I2C_TWIM->EVENTS_LASTTX = 0;
    I2C_TWIM->TASKS_RESUME = 1;
    I2C_TWIM->TXD.PTR = (uint32_t)i2c_tx;
    I2C_TWIM->TXD.MAXCNT = (uint32_t)n;
    I2C_TWIM->TASKS_STARTTX = 1;

    bool ok = i2c_wait(&I2C_TWIM->EVENTS_TXSTARTED, &I2C_TWIM->EVENTS_ERROR);
    I2C_TWIM->EVENTS_TXSTARTED = 0;
    if (ok) {
        ok = i2c_wait(&I2C_TWIM->EVENTS_LASTTX, &I2C_TWIM->EVENTS_ERROR);
        I2C_TWIM->EVENTS_LASTTX = 0;
    }

    if (stop || I2C_TWIM->EVENTS_ERROR || !ok) {
        I2C_TWIM->TASKS_STOP = 1;
        /* ⚠️ 여기가 Wire 에서 에러 탈출조차 없던 자리다. 반드시 상한을 둔다. */
        i2c_wait(&I2C_TWIM->EVENTS_STOPPED, 0);
        I2C_TWIM->EVENTS_STOPPED = 0;
    } else {
        I2C_TWIM->TASKS_SUSPEND = 1;
        i2c_wait(&I2C_TWIM->EVENTS_SUSPENDED, 0);
        I2C_TWIM->EVENTS_SUSPENDED = 0;
    }

    if (I2C_TWIM->EVENTS_ERROR) { I2C_TWIM->EVENTS_ERROR = 0; return 0; }
    return ok ? n : 0;
}

/* 읽기. 반환값: 실제로 받은 바이트 수. 0 이면 실패. */
static inline int i2c_read(uint8_t addr, uint8_t *buf, int n) {
    if (n <= 0 || n > I2C_BUF_MAX) return 0;

    I2C_TWIM->ADDRESS = addr;
    I2C_TWIM->EVENTS_ERROR = 0;
    I2C_TWIM->EVENTS_STOPPED = 0;
    I2C_TWIM->EVENTS_RXSTARTED = 0;
    I2C_TWIM->EVENTS_LASTRX = 0;
    I2C_TWIM->TASKS_RESUME = 1;
    I2C_TWIM->RXD.PTR = (uint32_t)i2c_rx;
    I2C_TWIM->RXD.MAXCNT = (uint32_t)n;
    I2C_TWIM->TASKS_STARTRX = 1;

    bool ok = i2c_wait(&I2C_TWIM->EVENTS_RXSTARTED, &I2C_TWIM->EVENTS_ERROR);
    I2C_TWIM->EVENTS_RXSTARTED = 0;
    if (ok) {
        ok = i2c_wait(&I2C_TWIM->EVENTS_LASTRX, &I2C_TWIM->EVENTS_ERROR);
        I2C_TWIM->EVENTS_LASTRX = 0;
    }

    I2C_TWIM->TASKS_STOP = 1;
    i2c_wait(&I2C_TWIM->EVENTS_STOPPED, 0);
    I2C_TWIM->EVENTS_STOPPED = 0;

    if (I2C_TWIM->EVENTS_ERROR) { I2C_TWIM->EVENTS_ERROR = 0; return 0; }
    if (!ok) return 0;

    int got = (int)I2C_TWIM->RXD.AMOUNT;
    if (got > n) got = n;
    for (int i = 0; i < got; i++) buf[i] = i2c_rx[i];
    return got;
}

/* 레지스터 하나를 정해 읽는 흔한 패턴 (쓰기 → repeated start → 읽기) */
static inline int i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, int n, bool use_stop) {
    if (i2c_write(addr, &reg, 1, use_stop) != 1) return 0;
    return i2c_read(addr, buf, n);
}

/* 주소가 응답하는지만 본다 (스캔용).
 * ⚠️ TWIM 은 0바이트 전송을 못 해서 더미 1바이트를 보낸다.
 *   대부분의 칩에서 그건 "레지스터 포인터를 0으로" 정도라 무해하지만,
 *   전부 그렇다고 보장은 못 한다. **진단용으로만** 쓸 것. */
static inline bool i2c_ping(uint8_t addr) {
    uint8_t dummy = 0;
    return i2c_write(addr, &dummy, 1, true) == 1;
}

#endif /* I2C_SAFE_H */
