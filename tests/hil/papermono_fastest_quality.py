"""SolarOS MicroPython, temporary file, no document/settings writes.

Run from USB: python /quality-trial.py hold 10
Patterns: hold, cycle, add, erase. Trial presents: 1..10, including initial.
Set display mode/timing and arm explicit cleanup BEFORE launching. One extra
white setup present consumes cleanup; VERIFY OTP full completion in its log.
At white setup and first image press Space; at last image press q to exit.
500ms spacing is diagnostic separation, never a firmware pacing change.
Completion/quality must be checked in the driver log and by the observer.
"""
import sys
import solaros

gfx = solaros.gfx


def specimen(x, y):
    gfx.color(gfx.BLACK)
    gfx.font(gfx.FONT_MONO_12)
    gfx.text(x, y + 14, "thin BLACK 0123456789")
    gfx.font(gfx.FONT_BOLD_20)
    gfx.text(x, y + 42, "BOLD BLACK")
    gfx.line(x, y + 54, x + 200, y + 54)
    gfx.fill_rect(x, y + 64, 200, 4)
    for row in range(4):
        for col in range(24):
            if (row + col) % 2 == 0:
                gfx.fill_rect(x + col * 6, y + 84 + row * 6, 6, 6)


def draw_initial():
    gfx.clear(gfx.WHITE)
    specimen(32, 40)  # Retained black: never redraw this region during steps.
    specimen(440, 40)  # Changed region: erase/rewrite for cycle/erase patterns.
    gfx.color(gfx.BLACK)
    gfx.font(gfx.FONT_MONO_20)
    gfx.text(32, 240, "LEFT: retained   RIGHT: changed")
    gfx.rect(16, 16, 768, 448)


def draw_step(pattern, index):
    # Every step changes this same marker, including hold-only trials.
    gfx.color(gfx.BLACK if index % 2 else gfx.WHITE)
    gfx.fill_rect(360, 48, 16, 16)
    if pattern == "cycle":
        gfx.color(gfx.WHITE)
        gfx.fill_rect(432, 32, 320, 140)
        if index % 2 == 0:
            specimen(440, 40)
    elif pattern == "erase":
        gfx.color(gfx.WHITE)
        gfx.fill_rect(440 + (index - 1) * 24, 40, 24, 140)
    elif pattern == "add":
        gfx.color(gfx.BLACK)
        gfx.font(gfx.FONT_MONO_20)
        gfx.text(440 + (index - 1) * 24, 210, "K")


def wait_for(keys):
    while not solaros.should_exit():
        key = gfx.getch(100)
        if key in keys:
            return key
        if key in (ord("q"), gfx.KEY_ESCAPE):
            return None
    return None


def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else "hold"
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    if pattern not in ("hold", "cycle", "add", "erase") or not 1 <= count <= 10:
        raise ValueError("pattern=hold/cycle/add/erase; presents=1..10")
    gfx.begin("display0")
    try:
        if gfx.size() != (800, 480):
            raise ValueError("requires landscape PaperMono 800x480")
        gfx.clear(gfx.WHITE)
        print("QUALITY setup=white request_ms=%d" % solaros.time.uptime_ms())
        gfx.present()
        print("QUALITY verify WHITE/full cleanup completion; Space starts, q exits")
        if wait_for((32,)) is None:
            return
        draw_initial()
        print("QUALITY pattern=%s ordinal=1 request_ms=%d" %
              (pattern, solaros.time.uptime_ms()))
        gfx.present()
        # No on-screen counter: old retained pixels and RAM classes stay fixed.
        print("QUALITY first image: inspect; Space continues, q exits")
        if count > 1 and wait_for((32,)) is None:
            return
        for index in range(1, count):
            if solaros.should_exit():
                return
            draw_step(pattern, index)
            print("QUALITY pattern=%s ordinal=%d request_ms=%d" %
                  (pattern, index + 1, solaros.time.uptime_ms()))
            gfx.present()
            solaros.time.sleep_ms(500)
        print("QUALITY requested=%d: inspect before cleanup; q exits" % count)
        wait_for((ord("q"),))
    finally:
        gfx.end()


if __name__ == "__main__":
    main()
