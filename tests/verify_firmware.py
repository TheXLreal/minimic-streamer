"""Run built ARM code in Unicorn with a minimal simulated TIMER/RADIO.

This checks firmware behavior, not RF interoperability, hardware timing, or USB enumeration.
Install: python -m pip install --target build/test-deps -r tests/requirements.txt
Run: python tests/verify_firmware.py
"""
import json
import pathlib
import struct
import sys
import unittest
import hashlib
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'build/test-deps'))
sys.path.insert(0, str(ROOT / 'tools'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_CPU_ARM_CORTEX_M33, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0
from elftools.elf.elffile import ELFFile
from package_firmware import read_hex, validate_core


def elf_data(core):
    with (ROOT / f'build/nrf5340dk/minimic_{core}.elf').open('rb') as file:
        elf = ELFFile(file)
        sections = [(s['sh_addr'], s.data()) for s in elf.iter_sections()
                    if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS']
        symbols = {s.name: (s['st_value'], s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()}
        members = {}
        for cu in elf.get_dwarf_info().iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag == 'DW_TAG_typedef' and die.attributes.get('DW_AT_name') and die.attributes['DW_AT_name'].value == b'minimic_ipc_t':
                    for member in die.get_DIE_from_attribute('DW_AT_type').iter_children():
                        if member.tag == 'DW_TAG_member':
                            members[member.attributes['DW_AT_name'].value.decode()] = member.attributes['DW_AT_data_member_location'].value
        return sections, symbols, members


def native_address_on_air(base, prefix):
    # nRF5340 PS packet configuration: ADDRESS bytes and bits are LSB first.
    # BALEN=3 truncates BASE bits 0..7. Interpret the resulting wire bits as
    # MSB-first bytes, as nRF24 does. This oracle is independent of wire.c.
    registers = [(base >> shift) & 255 for shift in (8, 16, 24)] + [prefix & 255]
    return bytes(int(f'{value:08b}'[::-1], 2) for value in registers)


class NetMachine:
    RADIO = 0x41008000
    MAILBOX = 0x20070000

    def __init__(self, enabled=1, usb=1, muted=0, epoch=0, pair_request=0, pair_desired=0,
                 pair_delay_us=0, debug_radio=0, trace_enabled=0, pair_address_order=0, pair_pcf=0, pair_start_sweep=0, pair_tx_dbm=0, high_voltage_ready=1):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33)
        for base, size in ((0x01000000, 0x40000), (0x21000000, 0x10000),
                           (self.MAILBOX, 0x10000), (0x41000000, 0x100000)):
            self.uc.mem_map(base, size)
        sections, self.syms, self.fields = elf_data('net')
        for address, data in sections:
            self.uc.mem_write(address, data)
        self.uc.reg_write(UC_ARM_REG_SP, 0x21010000)
        self.uc.reg_write(UC_ARM_REG_LR, 0x0103FFF1)
        self.now = 0
        self.time_step = 4
        self.packets = []
        self.tx_addresses = []
        self.tx_references = []
        self.tx_headers = []
        self.tx_power = []
        self.stop_condition = lambda: False
        self.rx_action = None
        self.write('magic', 0x4D4D5308)
        self.put(0x41004508, high_voltage_ready)
        for name, value in dict(enabled=enabled, usb_ready=usb, muted=muted, epoch=epoch,
                                pair_request=pair_request, pair_desired=pair_desired, pair_delay_us=pair_delay_us,
                                debug_radio=debug_radio, trace_enabled=trace_enabled,
                                pair_address_order=pair_address_order, pair_pcf=pair_pcf,
                                pair_start_sweep=pair_start_sweep, pair_tx_dbm=pair_tx_dbm).items():
            self.write(name, value)
        at = self.syms['platform_init'][0] & ~1
        self.uc.hook_add(UC_HOOK_CODE, self.code, begin=at, end=at)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.mmio, begin=self.RADIO, end=self.RADIO + 0xFFF)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.timer, begin=0x4100C040, end=0x4100C043)

    def word(self, address):
        return struct.unpack('<I', self.uc.mem_read(address, 4))[0]

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    def read(self, name):
        return self.word(self.MAILBOX + self.fields[name])

    def write(self, name, value):
        self.put(self.MAILBOX + self.fields[name], value)

    def feed(self, frame):
        head = self.read('head')
        self.uc.mem_write(self.MAILBOX + self.fields['frames'] + head * 84, frame)
        self.write('head', (head + 1) % 32)

    def code(self, uc, address, size, data):
        if address == (self.syms['platform_init'][0] & ~1):
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def timer(self, uc, access, address, size, value, data):
        self.now += self.time_step
        self.put(0x4100C540, self.now & 0xFFFFFFFF)
        if self.stop_condition():
            uc.emu_stop()

    def inject_rx(self, payload):
        ptr = self.word(self.RADIO + 0x504)
        self.uc.mem_write(ptr, payload)
        self.put(self.RADIO + 0x400, 1)  # CRCSTATUS
        self.put(self.RADIO + 0x104, 1)  # ADDRESS
        self.put(self.RADIO + 0x10C, 1)  # END
        self.put(self.RADIO + 0x110, 1)  # DISABLED
        self.put(0x4100C544, self.now + 130)  # READY -> CC[1]
        self.put(0x4100C548, self.now + 150)  # ADDRESS -> CC[2]
        self.put(0x4100C54C, self.now + 286)  # END -> CC[3]
        self.now += 286

    def mmio(self, uc, access, address, size, value, data):
        offset = address - self.RADIO
        if offset == 0 and value:  # TXEN
            ptr = self.word(self.RADIO + 0x504)
            header_len = 2 if self.word(self.RADIO + 0x514) else 0
            self.tx_headers.append(bytes(uc.mem_read(ptr, header_len)))
            self.packets.append((self.now, self.word(self.RADIO + 0x508), bytes(uc.mem_read(ptr + header_len, 32))))
            self.tx_addresses.append((self.word(self.RADIO + 0x51C), self.word(self.RADIO + 0x524)))
            self.tx_references.append(self.word(self.syms['pair_reference_us'][0]))
            self.tx_power.append((self.word(self.RADIO + 0x50C), self.word(0x41004500), self.word(0x41004508)))
            self.put(0x4100C544, self.now + 130)
            self.put(0x4100C548, self.now + 150)
            self.put(0x4100C54C, self.now + 286)
            self.now += 286
            self.put(self.RADIO + 0x10C, 1)
            self.put(self.RADIO + 0x110, 1)
        elif offset == 4 and value and self.rx_action:  # RXEN
            self.rx_action(self)
        elif offset == 0x10 and value:  # DISABLE
            self.put(self.RADIO + 0x550, 0)
            self.put(self.RADIO + 0x110, 1)

    def run(self, condition, instruction_budget=6000000):
        self.stop_condition = condition
        pc = self.uc.reg_read(UC_ARM_REG_PC)
        self.uc.emu_start(pc | 1 if pc else self.syms['main'][0], 0x0103FFF0, count=instruction_budget)
        if not condition():
            raise AssertionError(f'Emulator did not reach condition; now={self.now}, packets={len(self.packets)}')


class FirmwareTests(unittest.TestCase):
    def test_full_hop_cycle_and_duplicates(self):
        machine = NetMachine()
        a, b = bytes(range(28)), bytes(range(80, 108))
        machine.feed(a + b + bytes(x ^ y for x, y in zip(a, b)))
        machine.run(lambda: machine.read('tx_frames') >= 49)
        self.assertEqual(len(machine.packets), 49 * 5)
        hop = [24, 76, 18, 44, 66, 36, 16, 78, 68, 20, 62, 22, 38, 48, 42, 28]
        for frame_index in range(49):
            counter = frame_index % 48
            group = machine.packets[5 * frame_index:5 * frame_index + 5]
            self.assertEqual([p[2][0] for p in group], [0x18, 0x39, 0x19, 0x38, 0x1A])
            self.assertEqual([p[1] for p in group], [hop[(3 * counter + s) % 16] for s in [0, 0, 1, 1, 2]])
            self.assertEqual([p[2][1] for p in group], [(counter << 2) | low for low in [1, 0, 0, 1, 1]])
            for _, _, packet in group:
                self.assertEqual(len(packet), 32)
                self.assertEqual(packet[31], 0)
            self.assertEqual(group[0][2][2:30], group[3][2][2:30])
            self.assertEqual(group[1][2][2:30], group[2][2][2:30])
            self.assertEqual(group[4][2][2:30], bytes(x ^ y for x, y in zip(group[0][2][2:30], group[2][2][2:30])))
        self.assertEqual(machine.packets[0][2][2:30], a)
        self.assertEqual(machine.packets[2][2][2:30], b)
        # Original EN_AA=0 + ARC=0 selects ShockBurst without the ESB PCF.
        # Assert actual MMIO configuration, not just the software packet builder.
        self.assertEqual(machine.word(machine.RADIO + 0x514), 0)
        # SPI 12 34 56 78 -> on-air 78 56 34 12, MSB first per byte.
        self.assertEqual(machine.tx_addresses, [(0x2C6A1E00, 0x48)] * (49 * 5))
        for base, prefix in machine.tx_addresses:
            self.assertEqual(native_address_on_air(base, prefix), bytes.fromhex('78563412'))
        self.assertEqual(machine.read('faults'), 0)
        self.assertEqual(machine.read('late_frames'), 0)

    def test_usb_and_manual_disable(self):
        for kwargs in ({'usb': 0}, {'enabled': 0}):
            machine = NetMachine(**kwargs)
            machine.run(lambda: machine.now > 20000)
            self.assertEqual(machine.packets, [])
            self.assertEqual(machine.read('active'), 0)
        machine = NetMachine()
        machine.run(lambda: machine.read('tx_frames') >= 1)
        count = len(machine.packets)
        machine.write('enabled', 0)
        machine.run(lambda: machine.now > 16000 and machine.read('active') == 0)
        self.assertEqual(len(machine.packets), count)
        machine.write('enabled', 1)
        machine.run(lambda: machine.read('tx_frames') >= 2)
        self.assertEqual(len(machine.packets), count + 5)

    def test_mute_and_epoch_flush(self):
        for kwargs in ({'muted': 1}, {'epoch': 1}):
            machine = NetMachine(**kwargs)
            machine.feed(bytes([99]) * 84)
            machine.run(lambda: machine.read('tx_frames') >= 1)
            for _, _, packet in machine.packets:
                self.assertEqual(packet[2:30], bytes(28))
            self.assertEqual(machine.read('tail'), machine.read('head'))
            self.assertEqual(machine.read('epoch_ack'), machine.read('epoch'))

    def test_pair_cancel_and_timeout(self):
        machine = NetMachine(pair_request=2, pair_desired=0)
        machine.run(lambda: machine.read('tx_frames') >= 1)
        self.assertEqual(machine.read('pair_state'), 0)
        machine = NetMachine(pair_request=1, pair_desired=1)
        machine.rx_action = lambda m: (m.write('pair_desired', 0), m.write('pair_request', 2))
        machine.run(lambda: machine.read('pair_state') == 5)
        self.assertEqual(machine.packets, [])
        machine = NetMachine(pair_request=1, pair_desired=1)
        machine.time_step = 5000
        machine.run(lambda: machine.read('pair_state') == 4)
        self.assertEqual(machine.packets, [])

    def test_usb_descriptors_from_built_image(self):
        sections, symbols, _ = elf_data('app')
        address, length = symbols['config']
        config = next(data[address - base:address - base + length] for base, data in sections if base <= address < base + len(data))
        self.assertEqual(struct.unpack_from('<H', config, 2)[0], len(config))
        self.assertEqual(config[4], 4)
        interfaces, endpoints, associations, input_ids, sources, features = [], [], [], set(), [], []
        offset = 0
        while offset < len(config):
            size, kind = config[offset:offset + 2]
            self.assertGreaterEqual(size, 2)
            part = config[offset:offset + size]
            if kind == 4:
                interfaces.append((part[2], part[3], part[4], part[5]))
            if kind == 11:
                associations.append(tuple(part[2:7]))
            if kind == 5:
                endpoints.append((part[2], part[3] & 3, struct.unpack_from('<H', part, 4)[0]))
            if kind == 36 and interfaces and interfaces[-1][0] == 0:
                if part[2] == 2:
                    input_ids.add(part[3])
                elif part[2] == 3:
                    sources.append(part[7])
                elif part[2] == 6:
                    features.append(part)
            offset += size
        self.assertEqual(offset, len(config))
        self.assertEqual(interfaces, [(0, 0, 0, 1), (1, 0, 0, 1), (1, 1, 2, 1), (2, 0, 1, 2), (3, 0, 2, 10)])
        # If any IAD exists Windows stops using legacy audio grouping. Both
        # audio interfaces MUST be covered by one IAD, separately from CDC.
        self.assertEqual(associations, [(0, 2, 1, 1, 0), (2, 2, 2, 2, 0)])
        self.assertEqual(features, [], 'Do not advertise an empty Feature Unit to Windows')
        self.assertEqual(sources, [1])
        self.assertTrue(set(sources).issubset(input_ids), 'Output terminal source must exist')
        self.assertEqual(endpoints, [(8, 1, 196), (0x88, 1, 3), (0x81, 3, 8), (2, 2, 64), (0x82, 2, 64)])

    def test_pair_reply_and_confirmation(self):
        beacon = bytearray(32)
        beacon[0] = 0x10
        beacon[3:5] = b'\x1e\x03'
        beacon[8:12] = b'\xDE\xAD\xBE\xEF'
        beacon[12:22] = b'1234567890'
        beacon[28:30] = b'\x80\x43'
        machine = NetMachine(pair_request=1, pair_desired=1, trace_enabled=1)
        phase = [0]

        def receive(m):
            if phase[0] == 0:
                m.inject_rx(bytes(beacon))
                phase[0] = 1
            elif phase[0] == 1 and m.packets:
                confirmation = bytearray(beacon)
                confirmation[3] = 0x16
                confirmation[22:26] = bytes.fromhex('12345678')
                confirmation[29] = 0x4B
                m.inject_rx(bytes(confirmation))
                phase[0] = 2

        machine.rx_action = receive
        machine.run(lambda: machine.read('pair_count') == 1)
        self.assertEqual(len(machine.packets), 1)
        timestamp, channel, packet = machine.packets[0]
        self.assertGreaterEqual(timestamp - machine.tx_references[0], 5800)
        self.assertLess(timestamp - machine.tx_references[0], 5850)
        self.assertEqual(channel, 10)
        # Receiver SPI DE AD BE EF -> on-air EF BE AD DE. A repeated-byte
        # broadcast address cannot expose an accidental byte-order reversal.
        self.assertEqual(machine.tx_addresses, [(0xB57DF700, 0x7B)])
        self.assertEqual(native_address_on_air(*machine.tx_addresses[0]), bytes.fromhex('EFBEADDE'))
        reply = bytearray(32)
        reply[0:2] = b'\x30\x35'
        reply[4:6] = b'\x93\x14'
        reply[6:16] = b'1234567890'
        reply[16:20] = bytes.fromhex('12345678')
        reply[24:27] = b'\x04\x08\x05'
        self.assertEqual(packet, bytes(reply))
        self.assertEqual(machine.read('active'), 0)
        self.assertEqual(machine.read('trace_head'), 3)
        trace_base = machine.MAILBOX + machine.fields['trace']
        confirmation_trace = struct.unpack('<28I32s', machine.uc.mem_read(trace_base + 288, 144))
        self.assertEqual(confirmation_trace[0], 1)
        self.assertEqual(confirmation_trace[-1][3], 0x16, 'Confirmation must be logged before pairing handles it')
        machine.write('pair_desired', 0)
        machine.write('pair_request', 2)
        machine.run(lambda: machine.read('pair_state') == 5)

    def test_timer_wrap(self):
        machine = NetMachine()
        machine.now = 0xFFFFF000
        machine.run(lambda: machine.read('tx_frames') >= 4)
        starts = [packet[0] for packet in machine.packets[::5]]
        for first, second in zip(starts, starts[1:]):
            self.assertAlmostEqual(second - first, 6144, delta=16)
        self.assertEqual(machine.read('late_frames'), 0)

    def test_pair_reply_delay_scan_and_override(self):
        beacon = bytearray(32)
        beacon[3:5] = b'\x1e\x03'
        beacon[8:12] = bytes.fromhex('DEADBEEF')
        beacon[28:30] = b'\x80\x43'
        for fixed, expected in ((0, [5800, 5900, 5700, 6000, 5600]), (6200, [6200] * 5)):
            machine = NetMachine(pair_request=1, pair_desired=1, pair_delay_us=fixed)
            machine.rx_action = lambda m: m.inject_rx(bytes(beacon))
            machine.run(lambda: machine.read('pair_replies') >= 5)
            self.assertEqual(len(machine.packets), 5)
            # Each RX is injected when RXEN is written. The net core records
            # it on its next poll and prepares TX; allow this processing time.
            for packet, reference, delay in zip(machine.packets, machine.tx_references, expected):
                elapsed = packet[0] - reference
                self.assertGreaterEqual(elapsed, delay)
                self.assertLess(elapsed, delay + 100)
            self.assertEqual(machine.read('pair_last_delay'), expected[-1])
            self.assertEqual(machine.read('faults'), 0)

    def test_debugger_radio_and_hardware_trace(self):
        machine = NetMachine(usb=0, debug_radio=1, trace_enabled=1, pair_request=1, pair_desired=1)
        beacon = bytearray(32)
        beacon[3:5] = b'\x1e\x03'
        beacon[8:12] = bytes.fromhex('DEADBEEF')
        beacon[28:30] = b'\x80\x43'
        phase = [0]

        def receive(m):
            if phase[0] == 0:
                m.inject_rx(bytes(beacon)); phase[0] = 1

        machine.rx_action = receive
        machine.run(lambda: machine.read('pair_replies') == 1)
        self.assertEqual(machine.read('trace_head'), 2)
        # struct rf_trace_t: 28 words + 32-byte payload. The first RX record
        # must survive re-arming RX (which clears DMA and changes timestamps).
        base = machine.MAILBOX + machine.fields['trace']
        rx = struct.unpack('<28I32s', machine.uc.mem_read(base, 144))
        tx = struct.unpack('<28I32s', machine.uc.mem_read(base + 144, 144))
        self.assertEqual(rx[0], 1)
        self.assertEqual(rx[-1], bytes(beacon))
        self.assertEqual(tx[0], 3)
        self.assertEqual(tx[-1], machine.packets[0][2])
        self.assertEqual((tx[5] - tx[4]) & 0xFFFFFFFF, 136)
        self.assertEqual(tx[6], rx[5])
        self.assertGreaterEqual(tx[2] - rx[5], 5800)
        self.assertLess(tx[2] - rx[5], 5850)
        for record in (rx, tx):
            self.assertEqual(record[15:23], (0, 0, 0, 2, 0xFFFF, 0x11021, 0, 1))
        from capture_rf_swd import KEYS, text_record
        formatted_tx = dict(zip(KEYS, tx))
        formatted_tx['payload'] = tx[-1].hex().upper()
        lines = text_record(formatted_tx, 2).splitlines()
        self.assertEqual(len(lines), 3)
        self.assertIn('addr_spi=DEADBEEF addr_air=EFBEADDE', lines[0])
        self.assertIn('payload=' + bytes(machine.packets[0][2]).hex().upper(), lines[0])
        required = {'BASE0', 'BASE1', 'PREFIX0', 'PREFIX1', 'TXADDRESS', 'PCNF0', 'PCNF1',
                    'CRCCNF', 'CRCINIT', 'CRCPOLY', 'MODE', 'FREQUENCY'}
        self.assertTrue(required.issubset({word.split('=')[0] for word in lines[1].split()}))
        self.assertIn(f'beacon_rx_end_us={rx[5]}', lines[2])
        self.assertIn(f'tx_start_ready_us={tx[3]}', lines[2])
        self.assertIn(f'tx_end_us={tx[5]}', lines[2])
        self.assertEqual(machine.read('pair_deadline_us') - machine.read('pair_started_us'), 30000000)
        for offset, value in ((0x180, 0x80000000), (0x184, 0x80000001), (0x18C, 0x80000002)):
            self.assertEqual(machine.word(machine.RADIO + offset), value)
        self.assertEqual(machine.word(0x4100C0C4), 0x80000000)
        self.assertEqual(machine.word(0x4100C0C8), 0x80000001)
        self.assertEqual(machine.word(0x4100C0CC), 0x80000002)
        self.assertEqual(machine.word(0x4100F504), 7)

    def test_ready_start_sweep_and_high_voltage_power(self):
        machine = NetMachine(pair_request=1, pair_desired=1, trace_enabled=1,
                             pair_start_sweep=1, pair_tx_dbm=3)
        beacon = bytearray(32)
        beacon[3:5] = b'\x1e\x03'
        beacon[8:12] = bytes.fromhex('DEADBEEF')
        beacon[28:30] = b'\x80\x43'
        machine.rx_action = lambda m: m.inject_rx(bytes(beacon))
        machine.run(lambda: machine.read('pair_replies') == 36)
        self.assertEqual(machine.tx_power, [(0, 1, 1)] * 36,
                         '+3 dBm requires TXPOWER=0 and high radio voltage ready before every reply')
        base = machine.MAILBOX + machine.fields['trace']
        records = [struct.unpack('<28I32s', machine.uc.mem_read(base + index * 144, 144))
                   for index in range(machine.read('trace_head'))]
        replies = [record for record in records if record[0] == 3]
        targets = [5950, 6000, 6050, 6100, 6150, 6200]
        self.assertEqual(len(replies), 36)
        from capture_rf_swd import KEYS, text_record
        for index, record in enumerate(replies):
            step = (index // 3) % 6
            target = targets[step]
            self.assertEqual(record[24:28], (1, 1, target, step))
            actual_start = (record[3] - record[6]) & 0xFFFFFFFF
            self.assertGreaterEqual(actual_start, target)
            self.assertLessEqual(actual_start, target + 16)
            self.assertEqual((record[3] - record[2]) & 0xFFFFFFFF, 130)
            formatted = dict(zip(KEYS, record))
            formatted['payload'] = record[-1].hex().upper()
            line = text_record(formatted, index + 1)
            self.assertIn('configured_tx_dbm=3', line)
            self.assertIn(f'target_start_us={target}', line)
            self.assertIn(f'start_error_us={actual_start - target}', line)
        machine.write('pair_desired', 0)
        machine.write('pair_request', 2)
        machine.run(lambda: machine.read('pair_state') == 5)
        self.assertEqual(machine.word(0x41004500), 0, 'High voltage must be released after pairing')

    def test_high_voltage_not_ready_cancels_pairing(self):
        machine = NetMachine(pair_request=1, pair_desired=1, pair_tx_dbm=3, high_voltage_ready=0)
        machine.run(lambda: machine.read('pair_state') == 5)
        self.assertEqual(machine.read('faults'), 1)
        self.assertEqual(machine.read('pair_replies'), 0)
        self.assertEqual(machine.tx_power, [])
        self.assertEqual(machine.word(0x41004500), 0)

    def test_full_trace_buffer_never_stalls_radio(self):
        machine = NetMachine(trace_enabled=1, pair_request=1, pair_desired=1)
        machine.write('trace_head', 255)
        phase = [0]

        def receive(m):
            if phase[0] == 0:
                m.inject_rx(bytes([0xA5]) * 32)
                phase[0] = 1
            else:
                m.write('pair_desired', 0); m.write('pair_request', 2)

        machine.rx_action = receive
        machine.run(lambda: machine.read('pair_state') == 5)
        self.assertEqual(machine.read('trace_dropped'), 1)
        self.assertEqual(machine.read('trace_head'), 255)
        self.assertEqual(machine.read('faults'), 0)

    def test_diagnostic_alternative_receiver_address(self):
        beacon = bytearray(32)
        beacon[3:5] = b'\x1e\x03'
        beacon[8:12] = bytes.fromhex('DEADBEEF')
        beacon[28:30] = b'\x80\x43'
        for order, expected in ((0, 'EFBEADDE'), (1, 'DEADBEEF')):
            machine = NetMachine(pair_request=1, pair_desired=1, pair_address_order=order)
            phase = [0]

            def receive(m):
                if phase[0] == 0:
                    m.inject_rx(bytes(beacon)); phase[0] = 1

            machine.rx_action = receive
            machine.run(lambda: machine.read('pair_replies') == 1)
            self.assertEqual(native_address_on_air(*machine.tx_addresses[0]), bytes.fromhex(expected))
            self.assertEqual(machine.packets[0][2][16:20], bytes.fromhex('12345678'))

    def test_diagnostic_tx_pcf_keeps_receiver_legacy(self):
        beacon = bytearray(32)
        beacon[3:5] = b'\x1e\x03'
        beacon[28:30] = b'\x80\x43'
        for pcf, expected in ((0, b''), (1, b'\x00\x00'), (2, b'\x80\x00'), (3, b'\x80\x01')):
            machine = NetMachine(pair_request=1, pair_desired=1, pair_pcf=pcf)
            phase = [0]

            def receive(m):
                self.assertEqual(m.word(m.RADIO + 0x514), 0, 'Beacon RX must remain legacy')
                if phase[0] == 0:
                    m.inject_rx(bytes(beacon)); phase[0] = 1

            machine.rx_action = receive
            machine.run(lambda: machine.read('pair_replies') == 1)
            self.assertEqual(machine.tx_headers, [expected])
            self.assertEqual(machine.packets[0][2][16:20], bytes.fromhex('12345678'))

    def test_bounded_address_sweep_covers_mappings(self):
        import itertools
        beacon = bytearray(32)
        beacon[3:5] = b'\x1e\x03'
        beacon[8:12] = bytes.fromhex('DEADBEEF')
        beacon[28:30] = b'\x80\x43'
        machine = NetMachine(pair_request=1, pair_desired=1, pair_address_order=0xFFFFFFFF)
        machine.rx_action = lambda m: m.inject_rx(bytes(beacon))
        machine.run(lambda: machine.read('pair_replies') >= 49)
        actual = [native_address_on_air(*address) for address in machine.tx_addresses]
        permutations = set(bytes(p) for p in itertools.permutations(beacon[8:12]))
        bit_reversed = {bytes(int(f'{v:08b}'[::-1], 2) for v in p) for p in permutations}
        self.assertEqual(set(actual[:24]), permutations)
        self.assertEqual(set(actual[24:48]), bit_reversed)
        self.assertEqual(actual[48], actual[0])

    def test_receive_diagnostics(self):
        machine = NetMachine(pair_request=1, pair_desired=1, trace_enabled=1)
        phase = [0]

        def receive(m):
            if phase[0] < 2:
                m.inject_rx(bytes(32))
                if phase[0] == 0:
                    m.put(m.RADIO + 0x400, 0)  # Address match but invalid CRC.
                phase[0] += 1
            else:
                m.write('pair_desired', 0)
                m.write('pair_request', 2)

        machine.rx_action = receive
        machine.run(lambda: machine.read('pair_state') == 5)
        self.assertEqual(machine.read('rx_addresses'), 2)
        self.assertEqual(machine.read('rx_crc_bad'), 1)
        self.assertEqual(machine.read('rx_crc_ok'), 1)
        self.assertEqual(machine.read('rx_other'), 1)
        self.assertEqual(machine.read('beacons'), 0)
        self.assertEqual(machine.read('trace_head'), 2)
        base = machine.MAILBOX + machine.fields['trace']
        records = [struct.unpack('<28I32s', machine.uc.mem_read(base + index * 144, 144)) for index in range(2)]
        self.assertEqual([record[0] for record in records], [2, 1])
        self.assertEqual([record[-1] for record in records], [bytes(32), bytes(32)],
                         'RX logging must retain bad CRC and non-beacon packets without a byte-3 filter')

    def test_packaged_images(self):
        out = ROOT / 'dist/nrf5340dk'
        app, net, merged = [read_hex(out / name) for name in ('minimic_app.hex', 'minimic_net.hex', 'minimic_nrf5340dk.hex')]
        validate_core(app, 0, 0x100000, 0x20000000, 0x70000)
        validate_core(net, 0x1000000, 0x40000, 0x21000000, 0x10000)
        self.assertEqual(merged, {**app, **net})


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FirmwareTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    out = ROOT / 'dist/nrf5340dk'
    report = {
        'ran_utc': datetime.now(timezone.utc).isoformat(),
        'success': result.wasSuccessful(), 'tests': result.testsRun,
        'limitations': 'TIMER/RADIO are simulated. No hardware, USB host or receiver validation.',
        'images': {name: hashlib.sha256((out / name).read_bytes()).hexdigest()
                   for name in ('minimic_app.hex', 'minimic_net.hex', 'minimic_nrf5340dk.hex')},
    }
    (out / 'TESTS.json').write_text(json.dumps(report, indent=2) + '\n')
    sys.exit(0 if result.wasSuccessful() else 1)
