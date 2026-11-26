from datetime import datetime
from time import monotonic
from uuid import uuid4

from maix import err, image, video

from hevc_crop import with_visible_width


# The MaixCAM NV21 encoder buffer aligns its stride to 64 bytes.
VENC_STRIDE_ALIGNMENT = 64


def _capture_path(directory, kind, extension):
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    return directory / f"{kind}_{timestamp}_{uuid4().hex[:8]}.{extension}"


def save_photo(directory, frame):
    path = _capture_path(directory, "photo", "jpg")
    # Image.save selects the codec by extension.
    partial = path.with_name(f".{path.stem}.part.jpg")
    try:
        result = frame.save(str(partial), quality=95)
        if result != err.Err.ERR_NONE:
            raise OSError(f"JPEG save failed ({result}) for {path}")
        partial.replace(path)
        return path
    finally:
        partial.unlink(missing_ok=True)


class Recording:
    def __init__(self, directory, width, height, fps):
        self.path = _capture_path(directory, "video", "h265")
        self._partial = self.path.with_name(f".{self.path.name}.part")
        self._width = width
        self._height = height
        alignment = VENC_STRIDE_ALIGNMENT
        self._encoded_width = ((width + alignment - 1) // alignment) * alignment
        self._canvas = None
        if self._encoded_width != width:
            self._canvas = image.Image(
                self._encoded_width, height, image.Format.FMT_BGR888, image.COLOR_BLACK
            )
        self._has_sps = False
        self._encoder = video.Encoder(
            width=self._encoded_width,
            height=height,
            format=image.Format.FMT_YVU420SP,
            type=video.VideoType.VIDEO_H265_CBR,
            framerate=fps,
            block=True,
        )
        try:
            self._stream = self._partial.open("xb")
        except OSError:
            self._encoder = None
            raise
        self._frames_written = 0
        self.started = monotonic()

    def append(self, frame):
        # An aligned image bypasses the MMF branch that copies every row to
        # the same destination address when source width and stride differ.
        if self._canvas is not None:
            self._canvas.draw_image(0, 0, frame)
            frame = self._canvas
        nv21 = frame.to_format(image.Format.FMT_YVU420SP)
        packet = self._encoder.encode(nv21)
        if packet is None or not packet.is_valid():
            raise RuntimeError("H.265 encoder returned an empty packet")
        data = packet.to_bytes()
        if self._canvas is not None:
            data, has_sps = with_visible_width(data, self._encoded_width, self._width, self._height)
            self._has_sps |= has_sps
        if self._stream.write(data) != len(data):
            raise OSError(f"Short write while recording {self.path}")
        self._frames_written += 1

    def finish(self, *, keep=True):
        # Encoder owns one hardware channel and releases it on destruction.
        self._encoder = None
        try:
            self._stream.close()
            if keep and self._frames_written:
                if self._canvas is not None and not self._has_sps:
                    raise OSError("Missing HEVC SPS with the 1440x1080 conformance window")
                self._partial.replace(self.path)
                return self.path
            return None
        finally:
            self._partial.unlink(missing_ok=True)
