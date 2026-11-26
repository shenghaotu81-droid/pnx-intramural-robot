from contextlib import ExitStack
from pathlib import Path

from maix import app, camera, display, err, image, touchscreen

from capture_media import Recording, save_photo
from capture_ui import CaptureUI


WIDTH = 1440
HEIGHT = 1080
FPS = 30
CAMERA_FORMAT = image.Format.FMT_BGR888
OUTPUT_DIR = Path("/root/capture")


def take_photo(frame):
    if frame is None:
        return "No camera frame"
    try:
        path = save_photo(OUTPUT_DIR, frame)
    except Exception as exc:
        print(f"Photo save failed: {exc}", flush=True)
        return "Photo save failed"
    print(f"Photo saved: {path}", flush=True)
    return "Photo saved"


def finish_recording(recording, *, keep=True):
    try:
        path = recording.finish(keep=keep)
    except OSError as exc:
        print(f"Video save failed: {exc}", flush=True)
        return "Video save failed"
    if path is not None:
        print(f"Video saved: {path}", flush=True)
        return "Video saved"
    return "No video frames"


def capture_loop(cam, screen, touch):
    ui = CaptureUI(screen.width(), screen.height())
    recording = None
    try:
        while not app.need_exit():
            action = ui.touch_action(*touch.read())
            if action == "exit":
                break

            frame = cam.read(block_ms=150)
            if frame is not None:
                received = (frame.width(), frame.height(), frame.format())
                if received != (WIDTH, HEIGHT, CAMERA_FORMAT):
                    raise RuntimeError(f"Expected a 1440x1080 BGR888 frame, got {received}")

            if action == "photo":
                ui.notify(take_photo(frame))
            elif action == "record":
                if recording is not None:
                    ui.notify(finish_recording(recording))
                    recording = None
                elif frame is None:
                    ui.notify("No camera frame")
                else:
                    try:
                        recording = Recording(OUTPUT_DIR, WIDTH, HEIGHT, FPS)
                    except Exception as exc:
                        print(f"Recording start failed: {exc}", flush=True)
                        ui.notify("Recording start failed")

            if recording is not None and frame is not None:
                try:
                    recording.append(frame)
                except Exception as exc:
                    print(f"Recording failed: {exc}", flush=True)
                    finish_recording(recording, keep=False)
                    recording = None
                    ui.notify("Recording failed")

            started = recording.started if recording is not None else None
            result = screen.show(ui.render(frame, started))
            if result != err.Err.ERR_NONE:
                raise RuntimeError(f"Display update failed ({result})")
            del frame
    finally:
        if recording is not None:
            finish_recording(recording)


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    with ExitStack() as devices:
        cam = camera.Camera(WIDTH, HEIGHT, CAMERA_FORMAT, fps=FPS, buff_num=1)
        devices.callback(cam.close)
        screen = display.Display()
        devices.callback(screen.close)
        touch = touchscreen.TouchScreen()
        devices.callback(touch.close)
        capture_loop(cam, screen, touch)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
