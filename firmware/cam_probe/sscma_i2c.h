/* sscma_i2c.h — Grove Vision AI V2 를 I2C 로 직접 읽는다. 헤더 온리.
 *
 * ★ 왜 Seeed_Arduino_SSCMA 라이브러리를 안 쓰는가 (2026-09-01)
 *
 *   그 라이브러리는 한 번에 250바이트를 요청한다 (MAX_PL_LEN).
 *   그런데 이 코어의 Wire 버퍼는 **64바이트**다 (cores/nRF5/RingBuffer.h).
 *   그래서 requestFrom(250) 이 64밖에 못 받고, readBytes 가 나머지를 기다리며
 *   Stream 타임아웃(1초)까지 멈춘다. 4KB 응답이면 16번 × 1초 —
 *   AI.begin() 이 사실상 영원히 안 돌아온다. ESP32 기준으로 만든 라이브러리다.
 *
 *   여기서는 읽기를 **32바이트씩** 쪼갠다. 버퍼 안에 확실히 들어가고,
 *   우리가 필요한 건 박스 좌표 몇 개뿐이라 라이브러리 전체가 필요하지도 않다.
 *
 * ★ 모든 루프에 상한이 있다. 센서가 대답을 안 해도 절대 안 멈춘다.
 *   (한 번 멈춰서 원인 찾는 데 오래 걸렸다. 같은 일을 두 번 겪지 않게)
 *
 * 프로토콜 (Seeed 라이브러리에서 확인)
 *   쓰기  : [0x10][0x02][len_hi][len_lo][payload...]
 *   읽기  : [0x10][0x01][len_hi][len_lo][0][0]  → requestFrom(len)
 *   남은양: [0x10][0x03][0][0][0][0]            → requestFrom(2), 빅엔디안
 */
#ifndef SSCMA_I2C_H
#define SSCMA_I2C_H

#include <Arduino.h>
#include "i2c_safe.h"   /* ★ Wire 를 안 쓴다 — 이유는 i2c_safe.h 첫머리 */

#define SSCMA_ADDR        0x62
#define SSCMA_FEATURE     0x10
#define SSCMA_CMD_READ    0x01
#define SSCMA_CMD_WRITE   0x02
#define SSCMA_CMD_AVAIL   0x03

/* ★ Wire 버퍼(64) 안에 들어가는 최대치. 거래 횟수가 곧 지연이라 클수록 좋다.
 * 32로 시작했다가 56으로 올렸다 — 실측에서 센서 읽기가 1초 중 600ms 를 먹고 있었다.
 * 64로 꽉 채우지 않는 건 헤더/여유분 때문이다. */
#define SSCMA_CHUNK       56

/* 거래 사이 안정화 대기. Seeed 라이브러리 기본값이 2였는데
 * 거래가 많아 그대로 쓰면 대기만으로 프레임을 다 먹는다. 1로 줄였다.
 * ⚠️ 읽기가 불안정해지면 여기부터 되돌릴 것. */
#define SSCMA_WAIT_MS     1

/* 명령 한 줄 보내기. 우리 명령은 짧아서 한 패킷이면 끝난다. */
static inline void sscma_send(const char *s) {
    int len = strlen(s);
    if (len > I2C_BUF_MAX - 4) len = I2C_BUF_MAX - 4;   /* 우리 명령은 짧다 */
    uint8_t pkt[I2C_BUF_MAX];
    pkt[0] = SSCMA_FEATURE;
    pkt[1] = SSCMA_CMD_WRITE;
    pkt[2] = (uint8_t)(len >> 8);
    pkt[3] = (uint8_t)(len & 0xFF);
    for (int i = 0; i < len; i++) pkt[4 + i] = (uint8_t)s[i];
    delay(SSCMA_WAIT_MS);
    i2c_write(SSCMA_ADDR, pkt, len + 4, true);
}

/* 센서가 들고 있는 응답 바이트 수. 못 읽으면 0. */
static inline int sscma_available() {
    uint8_t cmd[6] = { SSCMA_FEATURE, SSCMA_CMD_AVAIL, 0, 0, 0, 0 };
    delay(SSCMA_WAIT_MS);
    if (i2c_write(SSCMA_ADDR, cmd, 6, true) != 6) return 0;

    delay(SSCMA_WAIT_MS);
    uint8_t b[2] = { 0, 0 };
    if (i2c_read(SSCMA_ADDR, b, 2) < 2) return 0;
    return ((int)b[0] << 8) | b[1];
}

/* 최대 max 바이트까지 읽어 dst 에 담는다. 실제로 담은 수를 반환.
 * ★ 32바이트씩 쪼개는 게 이 파일의 핵심이다. */
static inline int sscma_read(char *dst, int max) {
    int got = 0;
    /* 한 번 호출에 chunk 수를 제한한다 — 센서가 계속 뱉어도 여기서 안 눌러앉는다 */
    for (int guard = 0; guard < 64 && got < max; guard++) {
        int want = max - got;
        if (want > SSCMA_CHUNK) want = SSCMA_CHUNK;

        uint8_t cmd[6] = { SSCMA_FEATURE, SSCMA_CMD_READ,
                           (uint8_t)(want >> 8), (uint8_t)(want & 0xFF), 0, 0 };
        delay(SSCMA_WAIT_MS);
        if (i2c_write(SSCMA_ADDR, cmd, 6, true) != 6) break;

        delay(SSCMA_WAIT_MS);
        int k = i2c_read(SSCMA_ADDR, (uint8_t *)(dst + got), want);
        if (k <= 0) break;            /* 더 안 나온다 */
        got += k;
        if (k < want) break;
    }
    return got;
}

/* ── 응답에서 숫자만 뽑아내기 ──────────────────────────────
 * 정식 JSON 파서를 쓰지 않는다. ArduinoJson 을 끌어오면 라이브러리를
 * 버린 의미가 없어지고, 우리가 볼 건 "boxes":[[...]] 하나뿐이다. */

/* key 로 시작하는 배열을 찾아 [[a,b,c,...],[...]] 를 정수로 뽑는다.
 * out 에 (rows × cols) 로 채우고 행 수를 반환한다. */
static inline int sscma_parse_rows(const char *buf, const char *key,
                                   int16_t *out, int cols, int max_rows) {
    const char *p = strstr(buf, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p && *p != '[') p++;      /* 바깥 [ */
    if (!*p) return 0;
    p++;

    int rows = 0;
    while (*p && rows < max_rows) {
        while (*p == ' ' || *p == ',') p++;
        if (*p != '[') break;         /* 배열 끝 */
        p++;
        for (int c = 0; c < cols; c++) {
            while (*p == ' ' || *p == ',') p++;
            int v = 0, sign = 1;
            if (*p == '-') { sign = -1; p++; }
            if (*p < '0' || *p > '9') return rows;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            out[rows * cols + c] = (int16_t)(v * sign);
        }
        while (*p && *p != ']') p++;
        if (*p == ']') p++;
        rows++;
    }
    return rows;
}

#endif /* SSCMA_I2C_H */
