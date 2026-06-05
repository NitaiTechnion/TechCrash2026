import math
import random
import re
import threading
import time
import tkinter as tk
from dataclasses import dataclass

HEX_FRAME_RE = re.compile(r"^[0-9A-Fa-f]{3}$")
DEFAULT_PORT = "COM3"
DEFAULT_BAUD = 115200
RECONNECT_DELAY = 1.0

WIDTH = 720
HEIGHT = 860
HUD_HEIGHT = 56
BALL_RADIUS = 10
FLIPPER_LENGTH = 128
FLIPPER_RADIUS = 8
GRAVITY = 520.0
MAX_SPEED = 980.0
FRAME_MS = 16


@dataclass
class ControllerSnapshot:
    key0_pressed: bool = False
    key1_pressed: bool = False
    raw: str = "---"
    sw_bits: str = "0000000000"
    connected: bool = False
    status: str = "Starting serial thread..."


class ControllerState:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._snapshot = ControllerSnapshot()

    def update(self, **kwargs) -> None:
        with self._lock:
            for name, value in kwargs.items():
                setattr(self._snapshot, name, value)

    def snapshot(self) -> ControllerSnapshot:
        with self._lock:
            return ControllerSnapshot(**self._snapshot.__dict__)


def decode_packed_hex(frame: str) -> dict:
    packed = int(frame, 16)
    sw10 = (packed >> 2) & 0x03FF
    key1_pressed = bool((packed >> 1) & 0x1)
    key0_pressed = bool(packed & 0x1)
    sw_bits = "".join("1" if (sw10 >> bit) & 1 else "0" for bit in range(9, -1, -1))

    return {
        "raw": frame.upper(),
        "packed": packed,
        "sw10": sw10,
        "sw_bits": sw_bits,
        "key0_pressed": key0_pressed,
        "key1_pressed": key1_pressed,
    }


def serial_worker(controller: ControllerState, stop_event: threading.Event) -> None:
    try:
        import serial
    except ImportError:
        controller.update(status="Missing dependency: pyserial", connected=False)
        return

    while not stop_event.is_set():
        ser = None
        try:
            controller.update(
                status=f"Opening {DEFAULT_PORT} @ {DEFAULT_BAUD}...",
                connected=False,
                key0_pressed=False,
                key1_pressed=False,
            )
            ser = serial.Serial(port=DEFAULT_PORT, baudrate=DEFAULT_BAUD, timeout=0.2)
            controller.update(status="Serial connected", connected=True)

            while not stop_event.is_set():
                line = ser.readline()
                if not line:
                    continue

                text = line.decode("utf-8", errors="ignore").strip()
                if not text or not HEX_FRAME_RE.match(text):
                    continue

                decoded = decode_packed_hex(text)
                controller.update(
                    key0_pressed=decoded["key0_pressed"],
                    key1_pressed=decoded["key1_pressed"],
                    raw=decoded["raw"],
                    sw_bits=decoded["sw_bits"],
                    connected=True,
                    status="Serial connected",
                )
        except Exception as exc:
            if exc.__class__.__name__ == "SerialException":
                controller.update(
                    status=f"Serial disconnected; reconnecting in {RECONNECT_DELAY:.1f}s",
                    connected=False,
                    key0_pressed=False,
                    key1_pressed=False,
                )
                stop_event.wait(RECONNECT_DELAY)
            else:
                controller.update(status=f"Serial error: {exc}", connected=False)
                stop_event.wait(RECONNECT_DELAY)
        finally:
            if ser is not None and ser.is_open:
                ser.close()


class PinballGame:
    def __init__(self, root: tk.Tk, controller: ControllerState) -> None:
        self.root = root
        self.controller = controller
        self.canvas = tk.Canvas(root, width=WIDTH, height=HEIGHT, bg="#10131a", highlightthickness=0)
        self.canvas.pack()

        self.keyboard_left = False
        self.keyboard_right = False
        self.score = 0
        self.last_time = time.perf_counter()
        self.running = True

        self.left_pivot = (230.0, 760.0)
        self.right_pivot = (490.0, 760.0)
        self.bumpers = [
            {"x": 220.0, "y": 250.0, "r": 32.0, "value": 50, "color": "#f6c453"},
            {"x": 500.0, "y": 250.0, "r": 32.0, "value": 50, "color": "#6bd6ff"},
            {"x": 360.0, "y": 375.0, "r": 38.0, "value": 100, "color": "#ef6f6c"},
        ]

        self.ball_x = 0.0
        self.ball_y = 0.0
        self.ball_vx = 0.0
        self.ball_vy = 0.0
        self.reset_ball()

        root.title("CrashTech Pinball Controller")
        root.resizable(False, False)
        root.bind("<KeyPress>", self.on_key_press)
        root.bind("<KeyRelease>", self.on_key_release)
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.canvas.focus_set()

    def reset_ball(self) -> None:
        self.ball_x = WIDTH * 0.5
        self.ball_y = 520.0
        self.ball_vx = random.choice([-160.0, 160.0])
        self.ball_vy = 120.0

    def on_key_press(self, event: tk.Event) -> None:
        if event.keysym == "Left":
            self.keyboard_left = True
        elif event.keysym == "Right":
            self.keyboard_right = True
        elif event.keysym.lower() == "r":
            self.reset_ball()
        elif event.keysym == "Escape":
            self.close()

    def on_key_release(self, event: tk.Event) -> None:
        if event.keysym == "Left":
            self.keyboard_left = False
        elif event.keysym == "Right":
            self.keyboard_right = False

    def close(self) -> None:
        self.running = False
        self.root.destroy()

    def flipper_points(self, left_active: bool, right_active: bool) -> tuple:
        left_angle = math.radians(-32 if left_active else 18)
        right_angle = math.radians(212 if right_active else 162)

        left_tip = (
            self.left_pivot[0] + math.cos(left_angle) * FLIPPER_LENGTH,
            self.left_pivot[1] + math.sin(left_angle) * FLIPPER_LENGTH,
        )
        right_tip = (
            self.right_pivot[0] + math.cos(right_angle) * FLIPPER_LENGTH,
            self.right_pivot[1] + math.sin(right_angle) * FLIPPER_LENGTH,
        )
        return self.left_pivot, left_tip, self.right_pivot, right_tip

    def clamp_ball_speed(self) -> None:
        speed = math.hypot(self.ball_vx, self.ball_vy)
        if speed > MAX_SPEED:
            scale = MAX_SPEED / speed
            self.ball_vx *= scale
            self.ball_vy *= scale

    def collide_wall(self) -> None:
        left_wall = 54.0
        right_wall = WIDTH - 54.0
        top_wall = HUD_HEIGHT + 28.0

        if self.ball_x - BALL_RADIUS < left_wall:
            self.ball_x = left_wall + BALL_RADIUS
            self.ball_vx = abs(self.ball_vx) * 0.92
        elif self.ball_x + BALL_RADIUS > right_wall:
            self.ball_x = right_wall - BALL_RADIUS
            self.ball_vx = -abs(self.ball_vx) * 0.92

        if self.ball_y - BALL_RADIUS < top_wall:
            self.ball_y = top_wall + BALL_RADIUS
            self.ball_vy = abs(self.ball_vy) * 0.92

        if self.ball_y > HEIGHT + 80:
            self.reset_ball()

    def collide_bumper(self, bumper: dict) -> None:
        dx = self.ball_x - bumper["x"]
        dy = self.ball_y - bumper["y"]
        distance = math.hypot(dx, dy)
        min_distance = BALL_RADIUS + bumper["r"]
        if distance == 0 or distance >= min_distance:
            return

        nx = dx / distance
        ny = dy / distance
        self.ball_x = bumper["x"] + nx * min_distance
        self.ball_y = bumper["y"] + ny * min_distance
        dot = self.ball_vx * nx + self.ball_vy * ny
        self.ball_vx = self.ball_vx - 2.0 * dot * nx + nx * 260.0
        self.ball_vy = self.ball_vy - 2.0 * dot * ny + ny * 260.0
        self.score += bumper["value"]

    def collide_flipper(self, start: tuple, end: tuple, active: bool, direction: float) -> None:
        sx, sy = start
        ex, ey = end
        seg_x = ex - sx
        seg_y = ey - sy
        seg_len_sq = seg_x * seg_x + seg_y * seg_y
        if seg_len_sq == 0:
            return

        t = ((self.ball_x - sx) * seg_x + (self.ball_y - sy) * seg_y) / seg_len_sq
        t = max(0.0, min(1.0, t))
        closest_x = sx + seg_x * t
        closest_y = sy + seg_y * t
        dx = self.ball_x - closest_x
        dy = self.ball_y - closest_y
        distance = math.hypot(dx, dy)
        min_distance = BALL_RADIUS + FLIPPER_RADIUS
        if distance == 0 or distance >= min_distance:
            return

        nx = dx / distance
        ny = dy / distance
        self.ball_x = closest_x + nx * min_distance
        self.ball_y = closest_y + ny * min_distance

        dot = self.ball_vx * nx + self.ball_vy * ny
        if dot < 0:
            self.ball_vx -= 2.0 * dot * nx
            self.ball_vy -= 2.0 * dot * ny

        if active:
            self.ball_vx += direction * 310.0
            self.ball_vy -= 430.0
        else:
            self.ball_vx *= 0.9
            self.ball_vy *= 0.9

    def update_physics(self, dt: float, left_active: bool, right_active: bool) -> None:
        self.ball_vy += GRAVITY * dt
        self.ball_x += self.ball_vx * dt
        self.ball_y += self.ball_vy * dt

        self.collide_wall()
        for bumper in self.bumpers:
            self.collide_bumper(bumper)

        left_start, left_end, right_start, right_end = self.flipper_points(left_active, right_active)
        self.collide_flipper(left_start, left_end, left_active, 1.0)
        self.collide_flipper(right_start, right_end, right_active, -1.0)
        self.clamp_ball_speed()

    def draw_table(self, snapshot: ControllerSnapshot, left_active: bool, right_active: bool) -> None:
        self.canvas.delete("all")
        self.canvas.create_rectangle(0, 0, WIDTH, HEIGHT, fill="#10131a", outline="")
        self.canvas.create_rectangle(0, 0, WIDTH, HUD_HEIGHT, fill="#1d2430", outline="")

        serial_color = "#54d17a" if snapshot.connected else "#f6c453"
        self.canvas.create_text(
            24,
            18,
            anchor="w",
            fill="#edf2f7",
            font=("Segoe UI", 13, "bold"),
            text=f"Score {self.score}",
        )
        self.canvas.create_text(
            24,
            40,
            anchor="w",
            fill=serial_color,
            font=("Segoe UI", 9),
            text=f"{snapshot.status} | RAW {snapshot.raw} | SW {snapshot.sw_bits}",
        )
        self.canvas.create_text(
            WIDTH - 24,
            28,
            anchor="e",
            fill="#aeb7c2",
            font=("Segoe UI", 9),
            text="KEY1/Left = left paddle    KEY0/Right = right paddle",
        )

        self.canvas.create_line(54, HUD_HEIGHT + 24, 54, HEIGHT - 70, fill="#5b6575", width=5)
        self.canvas.create_line(WIDTH - 54, HUD_HEIGHT + 24, WIDTH - 54, HEIGHT - 70, fill="#5b6575", width=5)
        self.canvas.create_arc(54, HUD_HEIGHT - 42, WIDTH - 54, 230, start=0, extent=180, outline="#5b6575", width=5, style="arc")
        self.canvas.create_line(54, HEIGHT - 70, 280, HEIGHT - 8, fill="#5b6575", width=5)
        self.canvas.create_line(WIDTH - 54, HEIGHT - 70, 440, HEIGHT - 8, fill="#5b6575", width=5)

        for bumper in self.bumpers:
            x = bumper["x"]
            y = bumper["y"]
            r = bumper["r"]
            self.canvas.create_oval(x - r, y - r, x + r, y + r, fill=bumper["color"], outline="#fff4bd", width=3)
            self.canvas.create_oval(x - r * 0.45, y - r * 0.45, x + r * 0.45, y + r * 0.45, fill="#10131a", outline="")

        left_start, left_end, right_start, right_end = self.flipper_points(left_active, right_active)
        left_color = "#f6c453" if left_active else "#d6dbe3"
        right_color = "#6bd6ff" if right_active else "#d6dbe3"
        self.canvas.create_line(*left_start, *left_end, fill=left_color, width=18, capstyle=tk.ROUND)
        self.canvas.create_line(*right_start, *right_end, fill=right_color, width=18, capstyle=tk.ROUND)
        self.canvas.create_oval(left_start[0] - 12, left_start[1] - 12, left_start[0] + 12, left_start[1] + 12, fill="#8892a0", outline="")
        self.canvas.create_oval(right_start[0] - 12, right_start[1] - 12, right_start[0] + 12, right_start[1] + 12, fill="#8892a0", outline="")

        self.canvas.create_oval(
            self.ball_x - BALL_RADIUS,
            self.ball_y - BALL_RADIUS,
            self.ball_x + BALL_RADIUS,
            self.ball_y + BALL_RADIUS,
            fill="#ffffff",
            outline="#9ddcff",
            width=2,
        )

    def tick(self) -> None:
        if not self.running:
            return

        now = time.perf_counter()
        dt = min(0.034, now - self.last_time)
        self.last_time = now

        snapshot = self.controller.snapshot()
        left_active = self.keyboard_left or snapshot.key1_pressed
        right_active = self.keyboard_right or snapshot.key0_pressed

        self.update_physics(dt, left_active, right_active)
        self.draw_table(snapshot, left_active, right_active)
        self.root.after(FRAME_MS, self.tick)


def main() -> None:
    controller = ControllerState()
    stop_event = threading.Event()
    thread = threading.Thread(target=serial_worker, args=(controller, stop_event), daemon=True)
    thread.start()

    root = tk.Tk()
    game = PinballGame(root, controller)
    root.after(FRAME_MS, game.tick)

    try:
        root.mainloop()
    finally:
        stop_event.set()
        thread.join(timeout=1.0)


if __name__ == "__main__":
    main()
