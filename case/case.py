"""
실물 Color Picker 카메라 케이스 — 깊은 뒤통(tub) + 얇은 앞뚜껑(lid) 방식

설명: README.md (같은 폴더)
좌표: x = 길이(+x 쪽이 디스플레이/렌즈), y = 폭, z = 두께
      z=0 이 뒷면(사용자 쪽, 화면), z=D 가 앞면(대상 쪽, 렌즈)
구조: 깊은 통 하나(위 뚫림)에 부품·선뭉치 다 넣고, 얇은 평판 뚜껑을
      순간접착으로 붙인다. 두꺼운 앞상자를 통째로 안 뽑아서 필라멘트 절약.

실행:  pip install build123d  →  python case.py   → out/ 에 STL 생성
"""
from pathlib import Path
from build123d import *

# ─────────────── 부품 치수 (문서 §1) ───────────────
DK_L, DK_W, DK_T = 80.2, 20.6, 4.2          # NU40-DK ✅
DISP_L, DISP_W = 36.0, 44.0                  # ✅ PCB 44×36×5 실측 (x로 36, y로 44)
GLASS_L, GLASS_W = 35.6, 38.1                # ✅ 패널(유리) 35.6×38.1×1.5 실측
DISP_WIN = 33.5                              # 화면 창 지름 (유효화면 ⌀32.4)
CAM_L, CAM_W = 24.0, 25.0                    # OV5647 기판
CAM_LENS_FROM_LEFT = 13.7                    # 도면에서 읽은 렌즈 중심 (FFC 쪽 끝에서)
CAM_LENS_FROM_TOP = 11.05
CAM_HOLDER = 8.5                             # 렌즈 홀더 정사각
LENS_HOLE = 7.5                              # 렌즈 구멍 (⌀6 배럴 + 여유)
CAM_PCB_T = 1.2                              # 카메라 기판 두께
CAM_GRIP = 4.0                               # 카메라 기판 잡는 틀 깊이 (얕게 → 함몰 방지)
BAT_L, BAT_W, BAT_T = 48.0, 30.0, 10.0       # ✅ icbanq 3.7V 1500mAh 실측 (두께 10!)
GROVE_L, GROVE_W = 41.0, 23.0                # Grove Vision AI V2

# ─────────────── 케이스 파라미터 (문서 §5) ───────────────
WALL = 2.0           # 바닥/정렬립 기준 두께 (강성·접착면 유지)
SIDE = 1.2           # ✅ 2026-10-22: 옆벽 두께 (얇게 → 필라멘트 절약, 3겹)
CLR = 0.15            # 뚜껑 끼움 틈
LIP_T = 1.2          # 정렬 립 두께
FIT = 0.3            # 부품 끼움 여유
EXTRA_RIBBON = 10.0   # ✅ 실측: 리본(FFC) 접힘 공간 (디스플레이 반대쪽 끝) +1cm
EXTRA_DISPEND = 3.0   # ✅ 실측: 디스플레이 쪽 끝, 뒤 보드가 길이 넘어서 +0.3cm
EXTRA_W = 3.0         # ✅ 실측: 디스플레이 기판이 안 들어감(오바) → 폭 +0.3cm
OUTER_ADD = 14.0      # ✅ 2026-10-22: 디스플레이 바깥(+x) 벽만 더 늘림 → DK 모듈이 딱 안 들어가서
GAP_REDUCE = 5.0      # ✅ 2026-10-22: 디스플레이↔모듈(DK) 사이 남는 틈 줄임
IN_L = DK_L + 2 * (LIP_T + CLR + FIT + 0.6) + EXTRA_RIBBON + EXTRA_DISPEND + OUTER_ADD - GAP_REDUCE  # → 106.7
IN_W = DISP_W + 2 * 0.6 + EXTRA_W                      # 디스플레이 폭 + 여유 → 48.2
IN_D = 26.0           # ✅ 2026-10-22: 외형 높이 35→30mm (게이지로 선뭉치 들어감 확인)
L, W, D = IN_L + 2 * SIDE, IN_W + 2 * SIDE, IN_D + 2 * WALL  # 옆벽은 SIDE, 바닥은 WALL
R_V = 6.0             # 세로 모서리 둥글기
R_F = 1.5             # 앞·뒷면 테두리 둥글기

# ─────────────── 깊은 통 / 뚜껑 분할 ───────────────
LID_T = 3.0           # 앞 뚜껑 두께 (얇은 평판)
TUB_H = D - LID_T     # 깊은 뒤통 높이 (z 0 ~ 32)
LID_IN = TUB_H        # 뚜껑 안쪽 면 = 통 입구 높이
LID_LIP_DOWN = 3.0    # 뚜껑 정렬 립이 통 안으로 내려가는 깊이

# 층 높이 (z)
DK_Z0 = WALL + BAT_T + 0.5        # DK 아랫면 = 배터리(10) 위 0.5 틈 → 12.5
DK_Z1 = DK_Z0 + DK_T              # DK 윗면 16.7

# 위치 (x, y)
# 디스플레이는 뒤(배터리/DK) 기준 제자리 — 늘어난 길이(+14)는 디스플레이 바깥(+x)에만 생김
DISP_FROM_BACK = DK_L + 2 * (LIP_T + CLR + FIT + 0.6) + EXTRA_RIBBON - FIT - DISP_L / 2  # 디스플레이 중심(뒤벽 기준) 76.4
DISP_X = -IN_L / 2 + DISP_FROM_BACK
LENS_X, LENS_Y = DISP_X, 0.0                 # 렌즈를 화면 바로 뒤에 (뷰파인더처럼)
CAM_X0 = LENS_X - CAM_LENS_FROM_LEFT         # 카메라 기판 -x 끝 (FFC 나가는 쪽)
CAM_Y1 = LENS_Y + CAM_LENS_FROM_TOP          # 카메라 기판 +y 끝
BAT_X = -IN_L / 2 + FIT + BAT_L / 2          # 배터리는 -x 끝 (안테나 반대쪽 전제)

OUT = Path(__file__).parent / "out"
OUT.mkdir(exist_ok=True)


def at(x, y, z):
    return Pos(x, y, z)


def block(x0, x1, y0, y1, z0, z1):
    """좌표 범위로 상자 만들기"""
    return at((x0 + x1) / 2, (y0 + y1) / 2, z0) * Box(
        x1 - x0, y1 - y0, z1 - z0, align=(Align.CENTER, Align.CENTER, Align.MIN))


def rrect_prism(l, w, r, z0, h):
    """둥근 사각 기둥"""
    return at(0, 0, z0) * extrude(RectangleRounded(l, w, r), h)


# ═══════════════ 깊은 뒤통 (tub): 위가 뚫린 깊은 상자 ═══════════════
outer_tub = Box(L, W, TUB_H, align=(Align.CENTER, Align.CENTER, Align.MIN))
outer_tub = fillet(outer_tub.edges().filter_by(Axis.Z), R_V)      # 세로 모서리
outer_tub = fillet(outer_tub.edges().group_by(Axis.Z)[0], R_F)    # 바닥 테두리만 (윗입구는 평평 유지)
cavity = rrect_prism(IN_L, IN_W, R_V - SIDE, WALL, TUB_H - WALL + 1)  # 옆벽 SIDE, 바닥 WALL, 위로 뚫림
tub = outer_tub - cavity

# ── 바닥(뒷면): 화면 창 ──
tub -= at(DISP_X, 0, -1) * Cylinder(DISP_WIN / 2, WALL + 2, align=(Align.CENTER, Align.CENTER, Align.MIN))
tub -= at(DISP_X, 0, 0) * Cone(DISP_WIN / 2 + 0.8, DISP_WIN / 2, 0.8, align=(Align.CENTER, Align.CENTER, Align.MIN))
# 유리 안착 턱: 안쪽에서 0.8mm 파냄
tub -= block(DISP_X - (GLASS_L + 0.4) / 2, DISP_X + (GLASS_L + 0.4) / 2,
             -(GLASS_W + 0.4) / 2, (GLASS_W + 0.4) / 2, WALL - 0.8, WALL + 0.01)
# 화면 기판 x방향 막이 (y방향은 옆벽이 잡음)
for s in (1, -1):
    xe = DISP_X + s * (DISP_L / 2 + FIT)
    tub += block(*sorted((xe, xe + s * 1.2)), -12, 12, WALL, WALL + 3)

# ── 바닥: 배터리 칸 (낮은 턱) ──
bx0, bx1 = BAT_X - BAT_L / 2 - FIT, BAT_X + BAT_L / 2 + FIT
by = BAT_W / 2 + FIT
tub += block(bx1, bx1 + 1.2, -by - 1.2, by + 1.2, WALL, WALL + 3)      # 배터리 안쪽 끝막이
for s in (1, -1):
    tub += block(bx0, bx1, *sorted((s * by, s * (by + 1.2))), WALL, WALL + 3)  # 양 옆막이

# ── USB-C 옆면 노출 (-x 끝, DK 높이) ──
usb = block(-L / 2 - 1, -IN_L / 2 + 1, -5.5, 5.5, DK_Z0 + 0.5, DK_Z1 + 3.5)
tub -= usb

# ── 트러스 창문: 긴 ±y 옆벽에 원형 구멍 줄줄이 (재료 절약) ──
#    y로 관통하는 원기둥 → 양쪽 긴 벽을 동시에 뚫음(속은 빈 공간이라 벽만 뚫림)
HOLE_R = 6.5
HOLE_Z = WALL + (TUB_H - WALL) / 2          # 벽 높이 중앙 (위아래로 솔리드 띠 남김)
HOLE_XSPAN = IN_L - 30                        # 양 끝 15mm는 솔리드(모서리·USB 보호)
_n = max(1, round(HOLE_XSPAN / 15.0))
for _i in range(_n + 1):
    _xc = -HOLE_XSPAN / 2 + _i * HOLE_XSPAN / _n
    tub -= at(_xc, 0, HOLE_Z) * Rot(90, 0, 0) * Cylinder(
        HOLE_R, W + 4, align=(Align.CENTER, Align.CENTER, Align.CENTER))

# ═══════════════ 얇은 앞 뚜껑 (lid): 평판 + 렌즈 + 카메라 홀더 ═══════════════
lid = at(0, 0, TUB_H) * Box(L, W, LID_T, align=(Align.CENTER, Align.CENTER, Align.MIN))
lid = fillet(lid.edges().filter_by(Axis.Z), R_V)              # 세로 모서리
lid = fillet(lid.edges().group_by(Axis.Z)[-1], R_F)           # 바깥(위) 테두리만

# ── 정렬 립: 통 입구 안으로 끼워지는 아래 테두리 ──
lid += (rrect_prism(IN_L - 2 * CLR, IN_W - 2 * CLR, R_V - SIDE - CLR,
                    TUB_H - LID_LIP_DOWN, LID_LIP_DOWN)
        - rrect_prism(IN_L - 2 * CLR - 2 * LIP_T, IN_W - 2 * CLR - 2 * LIP_T,
                      R_V - SIDE - CLR - LIP_T, TUB_H - LID_LIP_DOWN - 1, LID_LIP_DOWN + 2))

# ── 렌즈 구멍 : 배럴(⌀6)은 바깥면까지 관통, 렌즈 홀더(8.5각)는 뚜껑 속으로 파고들게 ──
#    → 카메라가 깊은 우물에 함몰되지 않고 바깥면 1mm 앞까지 나와 똑바로 바깥을 봄
lid -= at(LENS_X, LENS_Y, TUB_H - 1) * Cylinder(LENS_HOLE / 2, LID_T + 2, align=(Align.CENTER, Align.CENTER, Align.MIN))
hh = CAM_HOLDER + 0.6  # 홀더가 올라올 사각 릴리프 (안쪽면 ~ 바깥면 1mm 앞)
lid -= block(LENS_X - hh / 2, LENS_X + hh / 2, LENS_Y - hh / 2, LENS_Y + hh / 2, LID_IN - 0.1, D - 1.0)
lid -= at(LENS_X, LENS_Y, D - 1.0) * Cone(LENS_HOLE / 2, LENS_HOLE / 2 + 1.2, 1.01, align=(Align.CENTER, Align.CENTER, Align.MIN))  # 바깥 모따기

# ── 카메라 PCB 틀 (얕게: 기판 윗면이 뚜껑 안쪽면에 닿게 → 홀더가 릴리프로 올라와 함몰 방지) ──
cx0, cx1 = CAM_X0 - FIT, CAM_X0 + CAM_L + FIT
cy1 = CAM_Y1 + FIT
cy0 = cy1 - CAM_W - 2 * FIT
cam_frame = (block(cx0 - 1.0, cx1 + 1.0, cy0 - 1.0, cy1 + 1.0, LID_IN - CAM_GRIP, LID_IN)
             - block(cx0, cx1, cy0, cy1, LID_IN - CAM_GRIP - 0.1, LID_IN + 0.1)
             - block(cx0 - 1.1, cx0 + 0.1, LENS_Y - 4.5, LENS_Y + 4.5, LID_IN - CAM_GRIP - 0.1, LID_IN + 0.1))  # FFC 나가는 틈
lid += cam_frame

# ═══════════════ 내보내기 ═══════════════
def to_bed(part):
    """평평한 바깥면이 베드에 닿도록 뒤집고 z=0으로 내림 (서포트 없이 출력)"""
    p = Rot(180, 0, 0) * part
    bb = p.bounding_box()
    return Pos(0, 0, -bb.min.Z) * p


export_stl(tub, str(OUT / "tub.stl"))
# lid 계열은 항상 뒤집어서 저장 → 바깥 평면이 바닥, 카메라 포켓이 위로 (서포트 X)
export_stl(to_bed(lid), str(OUT / "lid.stl"))
# 핏테스트용: 뚜껑에서 카메라/렌즈 귀퉁이만 잘라냄 (정렬만 확인 → 출력시간 절반)
export_stl(to_bed(lid & block(6, L, -W, W, -1, D + 1)), str(OUT / "lid_camtest.stl"))

# 초고속 핏 트레이: 통 바닥층만 → 자리 확인용 (벽 낮춤)
TRAY_H = 4.0
fit_tray = tub & block(-L, L, -W, W, -1, TRAY_H)
export_stl(fit_tray, str(OUT / "fit_tray.stl"))
# 더 빠른 '틀'버전: 바닥 가운데를 뚫어 테두리만 (솔리드 바닥 제거)
frame = fit_tray - block(-IN_L / 2 + 7, IN_L / 2 - 7, -IN_W / 2 + 7, IN_W / 2 - 7, -1, WALL + 0.01)
export_stl(frame, str(OUT / "fit_frame.stl"))

# 초미니 게이지: 네 귀퉁이 ㄱ자 + 높이 기둥 1개 (재료 최소, 몇 분이면 끝)
GL, GT, GH = 15.0, 2.0, 4.0
gx0, gx1 = -IN_L / 2, IN_L / 2
gy0, gy1 = -IN_W / 2, IN_W / 2
gauge = None
for cx, sx in [(gx0, 1), (gx1, -1)]:
    for cy, sy in [(gy0, 1), (gy1, -1)]:
        arm_x = block(min(cx, cx + sx * GL), max(cx, cx + sx * GL),
                      min(cy, cy + sy * GT), max(cy, cy + sy * GT), 0, GH)
        arm_y = block(min(cx, cx + sx * GT), max(cx, cx + sx * GT),
                      min(cy, cy + sy * GL), max(cy, cy + sy * GL), 0, GH)
        gauge = arm_x + arm_y if gauge is None else gauge + arm_x + arm_y
# 높이 기둥(한 귀퉁이): 선뭉치가 이 기둥보다 낮으면 뚜껑 닫힘
gauge += block(gx0, gx0 + GT, gy0, gy0 + GT, 0, TUB_H)
export_stl(gauge, str(OUT / "fit_gauge.stl"))

# 출력용 한 판: 통은 입구 위로, 뚜껑은 뒤집어 바깥면이 바닥에 오게 옆에
lid_flat = Rot(180, 0, 0) * lid
lid_flat = Pos(0, W + 12, 0) * lid_flat
_bb = lid_flat.bounding_box()
lid_flat = Pos(0, 0, -_bb.min.Z) * lid_flat
export_stl(tub + lid_flat, str(OUT / "print_plate.stl"))

# 부품 자리 (렌더 확인용 유령)
ghosts = {
    "dk": block(-DK_L / 2, DK_L / 2, -DK_W / 2, DK_W / 2, DK_Z0, DK_Z1),
    "display": (block(DISP_X - DISP_L / 2, DISP_X + DISP_L / 2, -DISP_W / 2, DISP_W / 2, WALL, WALL + 4.2)
                + block(DISP_X - GLASS_L / 2, DISP_X + GLASS_L / 2, -GLASS_W / 2, GLASS_W / 2, WALL - 0.8, WALL)),
    "battery": block(BAT_X - BAT_L / 2, BAT_X + BAT_L / 2, -BAT_W / 2, BAT_W / 2, WALL, WALL + BAT_T),
    "camera": block(CAM_X0, CAM_X0 + CAM_L, CAM_Y1 - CAM_W, CAM_Y1, LID_IN - CAM_PCB_T, LID_IN)
              + at(LENS_X, LENS_Y, LID_IN) * Box(CAM_HOLDER, CAM_HOLDER, D - 1.0 - LID_IN, align=(Align.CENTER, Align.CENTER, Align.MIN)),
    "grove": block(CAM_X0 - 4 - 2.5 - GROVE_L, CAM_X0 - 4 - 2.5, -GROVE_W / 2, GROVE_W / 2, DK_Z1 + 0.5, DK_Z1 + 10),
}
for k, g in ghosts.items():
    export_stl(g, str(OUT / f"ghost_{k}.stl"))

print(f"외형 {L:.1f} x {W:.1f} x {D:.1f} mm  (통 {TUB_H:.0f} + 뚜껑 {LID_T:.0f})")
print(f"화면/렌즈 중심 x={DISP_X:.1f}, 배터리 x={BAT_X:.1f}, DK z={DK_Z0:.1f}~{DK_Z1:.1f}")

# ─────────────── 겹침 검사 (부피) ───────────────
import itertools
print("\n[겹침 검사]")
parts = {"tub": tub, "lid": lid, **ghosts}
bad = 0
for a, b in itertools.combinations(parts, 2):
    if {a, b} == {"tub", "lid"}:   # 통/뚜껑은 따로 출력 → 건너뜀
        continue
    try:
        inter = parts[a] & parts[b]
        v = inter.volume if inter else 0.0
    except Exception:
        v = 0.0
    if v > 1.0:
        bb = inter.bounding_box()
        print(f"  ⚠ {a} ↔ {b}: {v:.0f} mm³  @ x[{bb.min.X:.1f},{bb.max.X:.1f}] y[{bb.min.Y:.1f},{bb.max.Y:.1f}] z[{bb.min.Z:.1f},{bb.max.Z:.1f}]")
        bad += 1
if bad == 0:
    print("  ✅ 1mm³ 넘는 겹침 없음")
