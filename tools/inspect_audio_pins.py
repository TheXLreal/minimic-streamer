"""Read Windows KS audio pin capabilities; makes no audio/control state changes."""
import ctypes as c
from ctypes import wintypes as w
import json
import struct
import uuid

kernel = c.WinDLL('kernel32', use_last_error=True)
kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
kernel.CreateFileW.restype = w.HANDLE
kernel.DeviceIoControl.argtypes = [w.HANDLE, w.DWORD, c.c_void_p, w.DWORD, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p]
kernel.DeviceIoControl.restype = w.BOOL
kernel.CloseHandle.argtypes = [w.HANDLE]
path = r'\\?\USB#VID_CAFE&PID_5340&MI_00#6&1facd2d&0&0000#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\GLOBAL'
handle = kernel.CreateFileW(path, 0xC0000000, 3, None, 3, 0, None)
if handle == c.c_void_p(-1).value:
    raise c.WinError(c.get_last_error())


def query(property_id, pin=None, group='8c134960-51ad-11cf-878a-94f801c10000'):
    request = uuid.UUID(group).bytes_le + struct.pack('<II', property_id, 1)
    if pin is not None:
        request += struct.pack('<II', pin, 0)
    source = c.create_string_buffer(request)
    result = c.create_string_buffer(16384)
    count = w.DWORD()
    if not kernel.DeviceIoControl(handle, 0x2F0003, source, len(request), result, len(result), c.byref(count), None):
        return {'error': c.get_last_error()}
    return result.raw[:count.value]


try:
    count = query(1)
    report = {'count': count if isinstance(count, dict) else struct.unpack('<I', count)[0], 'pins': []}
    if isinstance(report['count'], int):
        for pin in range(report['count']):
            info = {'pin': pin}
            for name, prop in [('dataflow', 2), ('communication', 7), ('category', 11), ('ranges', 3)]:
                raw = query(prop, pin)
                info[name] = raw if isinstance(raw, dict) else raw.hex()
            report['pins'].append(info)
    raw = query(2, group='720d4ac0-7533-11d0-a5d6-28db04c10000')
    report['connections'] = raw if isinstance(raw, dict) else raw.hex()
    raw = query(1, group='720d4ac0-7533-11d0-a5d6-28db04c10000')
    report['nodes'] = raw if isinstance(raw, dict) else [str(uuid.UUID(bytes_le=raw[i:i+16])) for i in range(8,len(raw),16)]
    print(json.dumps(report, indent=2))
finally:
    kernel.CloseHandle(handle)
