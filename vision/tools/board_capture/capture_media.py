from datetime import datetime
from time import monotonic
from uuid import uuid4

from maix import err, image, video


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
        self._encoder = video.Encoder(
            width=width,
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
        nv21 = frame.to_format(image.Format.FMT_YVU420SP)
        packet = self._encoder.encode(nv21)
        if packet is None or not packet.is_valid():
            raise RuntimeError("H.265 encoder returned an empty packet")
        if self._stream.write(packet.to_bytes()) != packet.size():
            raise OSError(f"Short write while recording {self.path}")
        self._frames_written += 1

    def finish(self, *, keep=True):
        # Encoder owns one hardware channel and releases it on destruction.
        self._encoder = None
        try:
            self._stream.close()
            if keep and self._frames_written:
                self._partial.replace(self.path)
                return self.path
            return None
        finally:
            self._partial.unlink(missing_ok=True)
