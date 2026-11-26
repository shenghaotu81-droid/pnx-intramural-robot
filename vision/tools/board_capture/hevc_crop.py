"""Set the visible width of an Annex B HEVC stream from the board encoder."""

import re


_START_CODE = re.compile(rb"\x00\x00\x00?\x01")
_SPS_NAL_TYPE = 33


def _read_ue(bits, offset):
    first_one = bits.find("1", offset)
    end = 2 * first_one - offset + 1
    if first_one < 0 or end > len(bits):
        raise ValueError("Truncated HEVC SPS")
    return int(bits[first_one:end], 2) - 1, end


def _write_ue(value):
    code = f"{value + 1:b}"
    return "0" * (len(code) - 1) + code


def _escape_rbsp(rbsp):
    data = bytearray()
    zeros = 0
    for byte in rbsp:
        if zeros == 2 and byte <= 3:
            data.append(3)
            zeros = 0
        data.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return bytes(data)


def _crop_sps(payload, encoded_width, visible_width, height):
    # Preserve Annex B trailing_zero_8bits outside the SPS payload.
    ebsp = payload.rstrip(b"\x00")
    rbsp = ebsp.replace(b"\x00\x00\x03", b"\x00\x00")
    bits = "".join(f"{byte:08b}" for byte in rbsp)
    if len(bits) < 104:
        raise ValueError("Truncated HEVC SPS")

    sublayers = int(bits[4:7], 2)
    # The VPS id, sublayer count and nesting flag take 8 bits. The general
    # profile_tier_level takes 96 bits, followed by optional sublayer fields.
    offset = 104
    if sublayers:
        flags = bits[offset:offset + 2 * sublayers]
        offset += 16 + 88 * flags[::2].count("1") + 8 * flags[1::2].count("1")

    _, offset = _read_ue(bits, offset)  # sps_seq_parameter_set_id
    chroma, offset = _read_ue(bits, offset)
    if chroma != 1:
        raise ValueError("Expected 4:2:0 HEVC encoder output")
    coded_width, offset = _read_ue(bits, offset)
    coded_height, offset = _read_ue(bits, offset)

    window_start = offset
    left = right = top = bottom = 0
    if bits[offset:offset + 1] == "1":
        left, offset = _read_ue(bits, offset + 1)
        right, offset = _read_ue(bits, offset)
        top, offset = _read_ue(bits, offset)
        bottom, offset = _read_ue(bits, offset)
    else:
        offset += 1

    # In a 4:2:0 SPS the offsets count two-pixel units in both directions.
    current_width = coded_width - 2 * (left + right)
    current_height = coded_height - 2 * (top + bottom)
    padding = encoded_width - visible_width
    stop_bit = bits.rfind("1")
    if (current_width, current_height) != (encoded_width, height) or stop_bit < offset:
        raise ValueError(f"Unexpected HEVC SPS ({current_width}x{current_height})")
    if padding <= 0 or padding % 2 or visible_width <= 0:
        raise ValueError("Invalid HEVC right crop")

    window = "1" + "".join(map(_write_ue, (left, right + padding // 2, top, bottom)))
    # Keep the original rbsp_stop_one_bit and restore byte alignment.
    updated = bits[:window_start] + window + bits[offset:stop_bit + 1]
    updated += "0" * (-len(updated) % 8)
    rbsp = bytes(int(updated[i:i + 8], 2) for i in range(0, len(updated), 8))
    return _escape_rbsp(rbsp) + payload[len(ebsp):]


def with_visible_width(packet, encoded_width, visible_width, height):
    """Rewrite each SPS in an encoder packet and report whether one was found."""
    starts = list(_START_CODE.finditer(packet))
    parts = []
    copied_through = 0
    for index, start in enumerate(starts):
        nal = start.end()
        end = starts[index + 1].start() if index + 1 < len(starts) else len(packet)
        if nal + 2 > end or ((packet[nal] >> 1) & 0x3F) != _SPS_NAL_TYPE:
            continue
        parts.append(packet[copied_through:nal + 2])
        parts.append(_crop_sps(packet[nal + 2:end], encoded_width, visible_width, height))
        copied_through = end
    if not parts:
        return packet, False
    parts.append(packet[copied_through:])
    return b"".join(parts), True
