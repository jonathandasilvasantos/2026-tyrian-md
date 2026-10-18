"""Shared binary helpers: little-endian reader, offset-table containers and
the text cipher (docs/data_formats.md, general conventions and §6.1)."""
import struct

CIPHER_KEY = (204, 129, 63, 255, 71, 19, 25, 62, 1, 99)


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def take(self, n):
        if self.pos + n > len(self.data):
            raise ValueError(f"read past end ({self.pos}+{n} > {len(self.data)})")
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def _unpack(self, fmt):
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))[0]

    def u8(self): return self._unpack("<B")
    def s8(self): return self._unpack("<b")
    def u16(self): return self._unpack("<H")
    def s16(self): return self._unpack("<h")
    def u32(self): return self._unpack("<I")
    def u16be(self): return self._unpack(">H")

    def array(self, fn, n):
        return [fn() for _ in range(n)]

    def pstring(self, size):
        """Pascal string in a fixed `size`-byte field (1 length byte included)."""
        raw = self.take(size)
        return raw[1:1 + min(raw[0], size - 1)].decode("latin-1")


def offset_table(data):
    """`u16 count; u32 offset[count]` -> list of (start, end) byte ranges;
    the last entry ends at EOF (lvllib.c:analyzeLevel and friends)."""
    r = Reader(data)
    offs = r.array(r.u32, r.u16())
    return [(o, offs[i + 1] if i + 1 < len(offs) else len(data)) for i, o in enumerate(offs)]


def decrypt(buf):
    """helptext.c:decrypt_string: XOR with the key, chained with the
    previous (still encrypted) byte, from the end."""
    b = bytearray(buf)
    for i in range(len(b) - 1, -1, -1):
        b[i] ^= CIPHER_KEY[i % 10]
        if i > 0:
            b[i] ^= b[i - 1]
    return bytes(b)


def read_encrypted_strings(data, start=0, end=None, limit=None):
    """Sequence of encrypted Pascal strings -> list of str."""
    r, out = Reader(data, start), []
    end = len(data) if end is None else end
    while r.pos < end and (limit is None or len(out) < limit):
        out.append(decrypt(r.take(r.u8())).decode("latin-1"))
    return out
