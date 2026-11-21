"""Compare Lab 3's CPU-visible DPIM transactions with a Lab 2 Verilator FST."""
import argparse
import subprocess
from itertools import chain, zip_longest
from pathlib import Path


def rtl_transactions(lines):
    # Only the wrapper clock and handshake signals are needed from the SoC dump.
    signals = {b'clk_i', b'req_i.req', b'req_i.we', b'req_i.be', b'req_i.addr',
               b'req_i.wdata', b'rsp_o.gnt', b'rsp_o.rvalid', b'rsp_o.rdata'}
    scope, ids = [], {}
    for line in lines:
        fields = line.split()
        if not fields:
            continue
        if fields[0] == b'$scope':
            scope.append(fields[2])
        elif fields[0] == b'$upscope':
            scope.pop()
        elif fields[0] == b'$var' and b'pim_i' in scope:
            name = b'.'.join(scope[scope.index(b'pim_i') + 1:] + [fields[4]])
            if name in signals:
                ids[fields[3]] = name
        elif fields[0] == b'$enddefinitions':
            break
    if set(ids.values()) != signals:
        raise ValueError('could not find the Lab 2 pim_i OBI signals')

    values, changes = {}, {}
    edge = 0
    request = None
    for line in chain(lines, [b'#end']):
        if line.startswith(b'#'):
            # At the rising edge the wrapper samples the values from just before
            # the clock transition. Apply simultaneous VCD changes only afterwards.
            if changes.get(b'clk_i') == 1 and values.get(b'clk_i') == 0:
                edge += 1
                if values[b'req_i.req'] and values[b'rsp_o.gnt']:
                    if request is not None:
                        raise ValueError('overlapping DPIM grants in RTL')
                    write = bool(values[b'req_i.we'])
                    request = (write, values[b'req_i.addr'], values[b'req_i.be'],
                               values[b'req_i.wdata'], edge)
                if values[b'rsp_o.rvalid']:
                    if request is None:
                        raise ValueError('unexpected DPIM response in RTL')
                    write, addr, be, wdata, grant = request
                    yield (write, addr, be, wdata if write else values[b'rsp_o.rdata'], edge - grant)
                    request = None
            values.update(changes)
            changes.clear()
        elif line.startswith(b'b'):
            data, code = line[1:].split()
            if code in ids:
                changes[ids[code]] = int(data, 2)
        elif line[:1] in (b'0', b'1') and (code := line[1:].strip()) in ids:
            changes[ids[code]] = int(line[:1])
    if request is not None:
        raise ValueError('RTL trace stopped before the last response')


def model_transactions(cosim, image):
    output = subprocess.check_output([cosim, image, '--trace'], text=True)
    for line in output.splitlines():
        if line.startswith(('load ', 'store ')):
            op, *fields = line.split()
            values = dict(item.split('=') for item in fields)
            yield (op == 'store', int(values['addr'], 16), int(values['be'], 16),
                   int(values['data'], 16), int(values['response']) - int(values['grant']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('waveform', type=Path)
    args = parser.parse_args()
    cosim = Path(__file__).resolve().parents[1] / 'build/cosim'
    model = list(model_transactions(cosim, args.image))
    if not model:
        raise SystemExit('firmware made no DPIM accesses')
    with subprocess.Popen(['fst2vcd', args.waveform], stdout=subprocess.PIPE) as reader:
        try:
            for index, (cpp, rtl) in enumerate(zip_longest(model, rtl_transactions(reader.stdout)), 1):
                if cpp != rtl:
                    raise SystemExit(f'transaction {index} differs (write, addr, be, data, latency)\n'
                                     f'C++ {cpp}\nRTL {rtl}')
        finally:
            reader.stdout.close()
        if reader.wait():
            raise SystemExit('fst2vcd could not read the waveform')
    print(f'PASS {args.image.stem} matched {len(model)} DPIM transactions and response latencies')


if __name__ == '__main__':
    main()
