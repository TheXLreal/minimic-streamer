"""Send two seconds of digital zero to the MiniMic MME render device.

Never opens the default device and never generates a tone or changes mixer settings.
Run alongside control-nrf5340.ps1 -Command STATUS to check mode=audio and the FIFO.
"""
import ctypes as c
from ctypes import wintypes as w
import time


class Caps(c.Structure):
    _fields_ = [('mid', w.WORD), ('pid', w.WORD), ('version', w.DWORD),
                ('name', w.WCHAR * 32), ('formats', w.DWORD), ('channels', w.WORD),
                ('reserved', w.WORD), ('support', w.DWORD)]


class Format(c.Structure):
    _pack_ = 1
    _fields_ = [('tag', w.WORD), ('channels', w.WORD), ('rate', w.DWORD),
                ('bytes_per_sec', w.DWORD), ('block', w.WORD), ('bits', w.WORD), ('extra', w.WORD)]


class Header(c.Structure):
    _fields_ = [('data', c.c_void_p), ('length', w.DWORD), ('recorded', w.DWORD),
                ('user', c.c_size_t), ('flags', w.DWORD), ('loops', w.DWORD),
                ('next', c.c_void_p), ('reserved', c.c_size_t)]


api = c.WinDLL('winmm')
api.waveOutGetDevCapsW.argtypes = [c.c_size_t, c.POINTER(Caps), w.UINT]
api.waveOutOpen.argtypes = [c.POINTER(w.HANDLE), w.UINT, c.POINTER(Format), c.c_size_t, c.c_size_t, w.DWORD]
for method in ('waveOutPrepareHeader', 'waveOutWrite', 'waveOutUnprepareHeader'):
    getattr(api, method).argtypes = [w.HANDLE, c.POINTER(Header), w.UINT]
api.waveOutReset.argtypes = [w.HANDLE]
api.waveOutClose.argtypes = [w.HANDLE]


def checked(result):
    if result:
        raise RuntimeError(f'Windows multimedia API returned {result}')


matches = []
for index in range(api.waveOutGetNumDevs()):
    caps = Caps()
    checked(api.waveOutGetDevCapsW(index, c.byref(caps), c.sizeof(caps)))
    if 'MiniMic' in caps.name:
        matches.append((index, caps.name))
if len(matches) != 1:
    raise RuntimeError(f'Expected one MiniMic render device, got {matches}')
index, name = matches[0]
handle = w.HANDLE()
format_pcm = Format(1, 2, 48000, 192000, 4, 16, 0)
checked(api.waveOutOpen(c.byref(handle), index, c.byref(format_pcm), 0, 0, 0))
pcm = c.create_string_buffer(192000 * 2)
header = Header(c.cast(pcm, c.c_void_p), len(pcm) - 1)
prepared = False
try:
    checked(api.waveOutPrepareHeader(handle, c.byref(header), c.sizeof(header)))
    prepared = True
    checked(api.waveOutWrite(handle, c.byref(header), c.sizeof(header)))
    deadline = time.monotonic() + 6
    while not header.flags & 1:
        if time.monotonic() >= deadline:
            raise RuntimeError('USB silence render did not finish')
        time.sleep(0.02)
    print(f'48 kHz stereo 16-bit digital silence rendered successfully to {name}.')
finally:
    api.waveOutReset(handle)
    if prepared:
        api.waveOutUnprepareHeader(handle, c.byref(header), c.sizeof(header))
    api.waveOutClose(handle)
