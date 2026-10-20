"""Capture RADIO hardware timestamps and packets through J2, without USB audio.

Uses the local nRF Util and the mailbox layout from the exact built net ELF.
Writes only firmware control fields in the reserved IPC RAM; no flash/UICR writes.
The debugger-only radio override is cleared on exit and always resets on boot.
"""
import argparse
import collections
from datetime import datetime, timezone
import hashlib
import json
import os
import re
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'build/test-deps'))
from elftools.elf.elffile import ELFFile
from package_firmware import read_hex

MAGIC = 0x4D4D5308
MAILBOX = 0x20070000
RECORD = struct.Struct('<28I32s')
KEYS = ('kind', 'channel', 'task_us', 'ready_us', 'address_us', 'end_us',
        'reference_us', 'base', 'prefix', 'crc', 'rssi', 'pcnf0', 'pcnf1', 'mode', 'header',
        'base1', 'prefix1', 'txaddress', 'crccnf', 'crcinit', 'crcpoly', 'txpower', 'rxaddresses', 'shorts',
        'vreqh', 'vreqhready', 'target_start_us', 'sweep_step', 'payload')
REGISTERS = (('BASE0', 'base'), ('BASE1', 'base1'), ('PREFIX0', 'prefix'), ('PREFIX1', 'prefix1'),
             ('TXADDRESS', 'txaddress'), ('PCNF0', 'pcnf0'), ('PCNF1', 'pcnf1'),
             ('CRCCNF', 'crccnf'), ('CRCINIT', 'crcinit'), ('CRCPOLY', 'crcpoly'),
             ('MODE', 'mode'), ('FREQUENCY', 'channel'), ('TXPOWER', 'txpower'),
             ('RXADDRESSES', 'rxaddresses'), ('SHORTS', 'shorts'),
             ('VREQH', 'vreqh'), ('VREQHREADY', 'vreqhready'))
START_SWEEP = (5950, 6000, 6050, 6100, 6150, 6200)


def layout():
    with (ROOT / 'build/nrf5340dk/minimic_net.elf').open('rb') as source:
        elf = ELFFile(source)
        for cu in elf.get_dwarf_info().iter_CUs():
            for die in cu.iter_DIEs():
                if die.tag == 'DW_TAG_typedef' and die.attributes.get('DW_AT_name') and die.attributes['DW_AT_name'].value == b'minimic_ipc_t':
                    target = die.get_DIE_from_attribute('DW_AT_type')
                    fields = {m.attributes['DW_AT_name'].value.decode(): m.attributes['DW_AT_data_member_location'].value
                              for m in target.iter_children() if m.tag == 'DW_TAG_member'}
                    return fields, target.attributes['DW_AT_byte_size'].value
    raise RuntimeError('Mailbox DWARF type not found')


class Probe:
    def __init__(self, serial, output):
        self.serial, self.output = serial, output
        self.env = dict(os.environ, NRFUTIL_HOME=str(ROOT / 'build/nrfutil-home'))
        self.fields, self.size = layout()
        self.counter = 0
        if self.size >= 65536 or self.size != self.fields['trace'] + 256 * RECORD.size:
            raise RuntimeError('Unexpected mailbox/trace layout')

    def run(self, *args):
        result = subprocess.run([str(ROOT / 'build/nrfutil/nrfutil.exe'), 'device', *map(str, args),
                                 '--serial-number', self.serial, '--core', 'application'],
                                env=self.env, capture_output=True, text=True, timeout=20)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def read(self):
        self.counter += 1
        path = self.output / f'.mailbox-{os.getpid()}-{self.counter}.hex'
        try:
            self.run('read', '--address', hex(MAILBOX), '--bytes', self.size, '--direct', '--to-file', path)
            data = read_hex(path)
            blob = bytes(data[MAILBOX + i] for i in range(self.size))
            if self.word(blob, 'magic') != MAGIC or self.word(blob, 'ready') != MAGIC:
                raise RuntimeError('Board mailbox ABI/network readiness does not match built ELF')
            return blob
        finally:
            path.unlink(missing_ok=True)

    def word(self, blob, name):
        return struct.unpack_from('<I', blob, self.fields[name])[0]

    def write(self, name, value):
        if name not in ('debug_radio', 'trace_enabled', 'trace_tail', 'enabled',
                        'pair_delay_us', 'pair_address_order', 'pair_pcf', 'pair_start_sweep', 'pair_tx_dbm',
                        'pair_desired', 'pair_request'):
            raise ValueError(f'Not a debugger control field: {name}')
        address = MAILBOX + self.fields[name]
        if not MAILBOX <= address < MAILBOX + 65536 - 3:
            raise ValueError('Control address outside reserved IPC RAM')
        self.run('write', '--address', hex(address), '--value', value & 0xFFFFFFFF, '--direct')


def diff(last, first):
    return (last - first) & 0xFFFFFFFF


def tx_address(record):
    logical = record['txaddress'] & 7
    base = record['base'] if logical == 0 else record['base1']
    prefixes = record['prefix'] if logical < 4 else record['prefix1']
    prefix = (prefixes >> (8 * (logical % 4))) & 255
    length = (record['pcnf1'] >> 16) & 7
    if not 2 <= length <= 4:
        raise ValueError('Unexpected RADIO BALEN')
    values = [(base >> shift) & 255 for shift in range((4 - length) * 8, 32, 8)] + [prefix]
    air = bytes(int(f'{value:08b}'[::-1], 2) for value in values)
    return air[::-1].hex().upper(), air.hex().upper()


def text_record(record, sequence):
    transmit = record['kind'] >= 3
    direction = 'TX' if transmit else 'RX'
    if transmit:
        spi, air = tx_address(record)
        packet = (f"TX seq={sequence} channel={record['channel']} addr_spi={spi} addr_air={air} "
                  f"payload={record['payload']} result={'END' if record['kind'] == 3 else 'TIMEOUT'}")
        if 'vreqh' in record:
            dbm = record['txpower'] & 255
            dbm = dbm - 256 if dbm >= 128 else dbm
            dbm += 3 if record['vreqh'] & record['vreqhready'] & 1 else 0
            packet += f" configured_tx_dbm={dbm} target_start_us={record['target_start_us']} sweep_step={record['sweep_step']}"
        timing = (f"TIME TX seq={sequence} beacon_rx_end_us={record['reference_us']} "
                  f"txen_us={record['task_us']} tx_start_ready_us={record['ready_us']} "
                  f"tx_address_us={record['address_us']} tx_end_us={record['end_us']} "
                  f"beacon_end_to_tx_start_us={diff(record['ready_us'], record['reference_us'])}")
        if record.get('target_start_us'):
            error = diff(record['ready_us'], record['reference_us']) - record['target_start_us']
            timing += f" target_start_us={record['target_start_us']} start_error_us={error}"
    else:
        packet = (f"RX seq={sequence} channel={record['channel']} crc={'OK' if record['kind'] == 1 else 'BAD'} "
                  f"rssi_dbm=-{record['rssi']} byte3=0x{record['payload'][6:8]} payload={record['payload']}")
        timing = (f"TIME RX seq={sequence} rxen_us={record['task_us']} ready_us={record['ready_us']} "
                  f"address_us={record['address_us']} received_end_us={record['end_us']}")
    registers = f'REG {direction} seq={sequence} ' + ' '.join(f'{label}=0x{record[key]:08X}' for label, key in REGISTERS if key in record)
    return '\n'.join((packet, registers, timing)) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial-number', required=True)
    parser.add_argument('--seconds', type=float, default=32)
    parser.add_argument('--delay-us', type=int, default=5800)
    parser.add_argument('--start-delay-sweep', action='store_true',
                        help='Sweep beacon END to READY/START 5950..6200 us; three replies per step; overrides delay-us')
    parser.add_argument('--tx-dbm', type=int, choices=(0, 3), default=0)
    parser.add_argument('--address-order', choices=('spi', 'air', 'sweep'), default='spi')
    parser.add_argument('--pcf', choices=('none', 'fixed', 'length32-bit0', 'length32-bit1'), default='none')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.serial_number.isdecimal() or not 1 <= args.seconds <= 60:
        parser.error('Use a numeric probe serial and duration 1..60 seconds')
    if args.delay_us != 0 and not 1000 <= args.delay_us <= 20000:
        parser.error('delay-us must be 0 (auto) or 1000..20000')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    probe = Probe(args.serial_number, output)
    initial = probe.read()
    all_records = []
    started = datetime.now(timezone.utc).isoformat()
    version = re.search(r'MINIMIC_FW_VERSION "([^"]+)"', (ROOT / 'nrf5340/version.h').read_text()).group(1)
    images = {name: hashlib.sha256((ROOT / 'dist/nrf5340dk' / name).read_bytes()).hexdigest()
              for name in ('minimic_app.hex', 'minimic_net.hex')}
    try:
        probe.write('trace_tail', probe.word(initial, 'trace_head'))
        probe.write('trace_enabled', 1)
        probe.write('enabled', 1)
        probe.write('pair_delay_us', args.delay_us)
        probe.write('pair_address_order', {'spi': 0, 'air': 1, 'sweep': 0xFFFFFFFF}[args.address_order])
        probe.write('pair_pcf', ('none', 'fixed', 'length32-bit0', 'length32-bit1').index(args.pcf))
        probe.write('pair_start_sweep', int(args.start_delay_sweep))
        probe.write('pair_tx_dbm', args.tx_dbm)
        probe.write('debug_radio', 1)
        probe.write('pair_desired', 1)
        probe.write('pair_request', probe.word(initial, 'pair_request') + 1)
        deadline = time.monotonic() + args.seconds
        window = probe.read()
        window_info = {'host_utc': datetime.now(timezone.utc).isoformat(),
                       'network_start_us': probe.word(window, 'pair_started_us'),
                       'network_deadline_us': probe.word(window, 'pair_deadline_us')}
        (output / 'STARTED.json').write_text(json.dumps(window_info, indent=2) + '\n', encoding='utf-8')
        with (output / 'packets.jsonl').open('w', encoding='utf-8') as log, \
             (output / 'pairing.log').open('w', encoding='utf-8') as readable:
            readable.write(f'MiniMic nRF5340 DK firmware {version}; receiver: Nucleus 7\n')
            readable.write(f'Capture UTC: {started}\nParameters: tx_dbm={args.tx_dbm}; '
                           f'address_order={args.address_order}; tx_pcf={args.pcf}; '
                           f'start_delay_sweep={args.start_delay_sweep}; fallback_txen_delay_us={args.delay_us}\n')
            if args.start_delay_sweep:
                readable.write(f'READY/START targets (us): {START_SWEEP}; three replies per step, cyclic. '
                               'TXEN is scheduled 130 us earlier; actual timing/error is logged per reply.\n')
            readable.write('nRF5340 +3 dBm uses TXPOWER=0 and VREQH=1 after VREQHREADY=1. '
                           'This records the configured power, not a measured antenna output.\n')
            readable.write(f"PAIR WINDOW: start_us={window_info['network_start_us']} "
                           f"deadline_us={window_info['network_deadline_us']} (network TIMER0)\n")
            readable.write('READY/ADDRESS/END timestamps are hardware DPPI captures into network TIMER0 CC[1..3].\n'
                           'TX start is READY -> START shortcut, not the earlier CPU TXEN task.\n'
                           'Register snapshots are taken after packet END, before changing that packet configuration.\n'
                           'RX includes every completed addressed packet on the tuned channel, CRC good or bad, without payload filtering.\n'
                           'addr_spi: nRF24 register byte order; addr_air: first-to-last bytes, MSB-first notation.\n'
                           'TX END reports local completion, not receiver acknowledgement.\n'
                           'Image SHA256: ' + json.dumps(images) + '\n\n')
            while True:
                snapshot = probe.read()
                head, tail = probe.word(snapshot, 'trace_head'), probe.word(snapshot, 'trace_tail')
                batch = []
                while tail != head:
                    values = RECORD.unpack_from(snapshot, probe.fields['trace'] + tail * RECORD.size)
                    record = dict(zip(KEYS, values))
                    record['payload'] = record['payload'].hex().upper()
                    record['ready_to_address_us'] = diff(record['address_us'], record['ready_us'])
                    record['address_to_end_us'] = diff(record['end_us'], record['address_us'])
                    if record['kind'] >= 3:
                        record['txen_to_ready_us'] = diff(record['ready_us'], record['task_us'])
                        record['beacon_end_to_tx_address_us'] = diff(record['address_us'], record['reference_us'])
                        record['beacon_end_to_tx_start_us'] = diff(record['ready_us'], record['reference_us'])
                        if record['target_start_us']:
                            record['start_error_us'] = record['beacon_end_to_tx_start_us'] - record['target_start_us']
                    log.write(json.dumps(record) + '\n')
                    readable.write(text_record(record, len(all_records) + 1))
                    all_records.append(record)
                    batch.append(record)
                    tail = (tail + 1) % 256
                log.flush()
                readable.flush()
                if batch:
                    probe.write('trace_tail', head)
                print(f"packets={len(all_records)} pair={probe.word(snapshot, 'pair_state')} "
                      f"paired={probe.word(snapshot, 'pair_count')} replies={probe.word(snapshot, 'pair_replies')} "
                      f"lost_trace={probe.word(snapshot, 'trace_dropped')}", flush=True)
                if time.monotonic() >= deadline:
                    break
                time.sleep(min(2, max(0, deadline - time.monotonic())))
        metrics = {}
        for key in ('address_to_end_us', 'txen_to_ready_us', 'beacon_end_to_tx_address_us', 'beacon_end_to_tx_start_us'):
            values = [r[key] for r in all_records if key in r and (key == 'address_to_end_us' or r['kind'] == 3)]
            if values:
                metrics[key] = {'min': min(values), 'max': max(values), 'median': statistics.median(values)}
        summary = {
            'started_utc': started, 'firmware': version, 'images_sha256': images,
            'window': window_info, 'requested_delay_us': args.delay_us,
            'requested_tx_dbm': args.tx_dbm,
            'start_delay_sweep_us': list(START_SWEEP) if args.start_delay_sweep else [],
            'replies_per_step': 3 if args.start_delay_sweep else None,
            'address_order_in_beacon': args.address_order,
            'tx_packet_control_field': args.pcf,
            'counts': dict(collections.Counter(r['kind'] for r in all_records)),
            'status': {name: probe.word(snapshot, name) for name in
                       ('pair_state', 'pair_count', 'beacons', 'pair_replies', 'trace_dropped', 'faults', 'late_frames',
                        'rx_crc_ok', 'rx_crc_bad', 'rx_other')},
            'timing': metrics,
        }
        first_beacon = next((r for r in all_records if r['kind'] == 1 and r['payload'][6:10] in ('1E03', '1603')), None)
        if first_beacon:
            remaining = diff(window_info['network_deadline_us'], first_beacon['end_us']) / 1e6
            summary['seconds_remaining_at_first_valid_beacon'] = remaining
            summary['at_least_10_seconds_of_receiver_packets_before_deadline'] = 10 <= remaining <= 30
        summary['rx_byte3_counts'] = dict(collections.Counter(r['payload'][6:8] for r in all_records if r['kind'] == 1))
        good_rx = [r for r in all_records if r['kind'] == 1]
        if good_rx:
            summary['rx_rssi_dbm'] = {'min': min(-r['rssi'] for r in good_rx),
                                      'max': max(-r['rssi'] for r in good_rx)}
        summary['sweep_results'] = []
        for step, target in enumerate(START_SWEEP):
            replies = [r for r in all_records if r['kind'] == 3 and r['target_start_us'] == target]
            if replies:
                actual = [r['beacon_end_to_tx_start_us'] for r in replies]
                summary['sweep_results'].append({'step': step, 'target_start_us': target, 'replies': len(replies),
                    'actual_start_min_us': min(actual), 'actual_start_max_us': max(actual),
                    'error_min_us': min(actual) - target, 'error_max_us': max(actual) - target})
        summary['confirmation_context'] = []
        preceding_tx = None
        for seq, record in enumerate(all_records, 1):
            if record['kind'] == 3:
                preceding_tx = (seq, record)
            elif record['kind'] == 1 and record['payload'][6:8] == '16':
                context = {'rx_seq': seq, 'rx_end_us': record['end_us']}
                if preceding_tx:
                    tx_seq, tx = preceding_tx
                    context.update({'preceding_tx_seq': tx_seq, 'preceding_target_start_us': tx['target_start_us'],
                                    'preceding_sweep_step': tx['sweep_step'],
                                    'preceding_tx_end_to_rx_end_us': diff(record['end_us'], tx['end_us'])})
                summary['confirmation_context'].append(context)
        with (output / 'pairing.log').open('a', encoding='utf-8') as readable:
            readable.write('\nSUMMARY\n' + json.dumps(summary, indent=2) + '\n')
        (output / 'SUMMARY.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(summary, indent=2), flush=True)
    finally:
        probe.write('debug_radio', 0)
        for name in ('pair_delay_us', 'pair_address_order', 'pair_pcf', 'pair_start_sweep', 'pair_tx_dbm', 'enabled'):
            probe.write(name, probe.word(initial, name))


if __name__ == '__main__':
    main()
