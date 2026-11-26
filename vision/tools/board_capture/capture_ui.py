from time import monotonic

from maix import image


BACKGROUND = image.Color.from_rgb(18, 23, 31)
PHOTO_BUTTON = image.Color.from_rgb(20, 86, 127)
NEUTRAL_BUTTON = image.Color.from_rgb(40, 50, 64)
RECORDING_RED = image.Color.from_rgb(169, 40, 53)
RECORDING_TEXT = image.Color.from_rgb(255, 133, 141)


class CaptureUI:
    def __init__(self, width, height):
        self._canvas = image.Image(width, height)
        self._width = width
        self._height = height
        self._margin = max(4, min(12, width // 32))
        self._font_scale = max(0.55, min(1.0, width / 480))
        self._status_y = self._margin
        self._preview_top = self._status_y + 28

        gap = self._margin
        button_width = (width - 2 * self._margin - 2 * gap) // 3
        button_height = max(36, min(60, height // 7))
        button_y = height - self._margin - button_height
        self._buttons = {
            action: (
                self._margin + i * (button_width + gap),
                button_y,
                button_width,
                button_height,
            )
            for i, action in enumerate(("photo", "record", "exit"))
        }
        self._preview_height = button_y - gap - self._preview_top
        self._was_pressed = False
        self._notice = ""
        self._notice_until = 0.0

    def touch_action(self, x, y, pressed):
        new_press = pressed and not self._was_pressed
        self._was_pressed = pressed
        if new_press:
            for action, (left, top, width, height) in self._buttons.items():
                if left <= x < left + width and top <= y < top + height:
                    return action
        return None

    def notify(self, message):
        self._notice = message
        self._notice_until = monotonic() + 4

    def render(self, frame, recording_since):
        img = self._canvas
        img.draw_rect(0, 0, self._width, self._height, BACKGROUND, -1)
        if frame is None:
            self._draw_centered(
                "Waiting for camera", (0, self._preview_top, self._width, self._preview_height)
            )
        else:
            # Keep the whole 4:3 frame inside the preview area.
            scale = min((self._width - 2 * self._margin) // 4, self._preview_height // 3)
            w, h = 4 * scale, 3 * scale
            x = (self._width - w) // 2
            y = self._preview_top + (self._preview_height - h) // 2
            img.draw_image(x, y, frame.resize(w, h))

        now = monotonic()
        recording = recording_since is not None
        if recording:
            minutes, seconds = divmod(int(now - recording_since), 60)
            self._draw_text(
                self._margin, self._status_y, f"REC {minutes:02d}:{seconds:02d}", RECORDING_TEXT
            )
        elif frame is not None:
            self._draw_text(self._margin, self._status_y, f"{frame.width()} x {frame.height()}")
        if now < self._notice_until:
            width = self._text_size(self._notice).width()
            self._draw_text(self._width - self._margin - width, self._status_y, self._notice)

        self._draw_button("photo", "Photo", PHOTO_BUTTON)
        self._draw_button(
            "record",
            "Stop REC" if recording else "Start REC",
            RECORDING_RED if recording else NEUTRAL_BUTTON,
        )
        self._draw_button("exit", "Exit", NEUTRAL_BUTTON)
        return img

    def _draw_button(self, action, label, fill):
        bounds = self._buttons[action]
        self._canvas.draw_rect(*bounds, fill, -1)
        self._draw_centered(label, bounds)

    def _draw_centered(self, label, bounds):
        x, y, width, height = bounds
        size = self._text_size(label)
        self._draw_text(
            x + (width - size.width()) // 2, y + (height - size.height()) // 2, label
        )

    def _text_size(self, label):
        return image.string_size(label, scale=self._font_scale, thickness=-1)

    def _draw_text(self, x, y, label, color=image.COLOR_WHITE):
        self._canvas.draw_string(x, y, label, color, scale=self._font_scale, wrap=False)
