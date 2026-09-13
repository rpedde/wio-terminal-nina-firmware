#!/usr/bin/env python3
"""Summarize phase5_heap.py's dedicated-RTL-UART samples without user secrets."""
import argparse
import json
from pathlib import Path
import re
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
text = args.log.read_text(errors='replace')
samples = {label: {'free_bytes': int(free), 'minimum_ever_free_bytes': int(minimum)}
           for label, free, minimum in re.findall(r'HEAP_SAMPLE (\w+) (\d+) (\d+)', text)}
required = ['before_tls', 'tls_open', 'first_tls_closed', 'baseline', 'after_recovery']
required += ['warmup_' + str(i) for i in range(1, 11)]
required += ['https_' + str(i) for i in range(1, 101)]
required += ['rejection_batch_' + str(i) for i in range(1, 11)]
required += ['settled_' + str(i) for i in range(10, 131, 10)]
missing = sorted(set(required) - samples.keys())
if missing:
    raise SystemExit('Missing measurements: ' + ', '.join(missing))
free = lambda label: samples[label]['free_bytes']
cycles = [free('https_' + str(i)) for i in range(1, 101)]
result = {
    'measurement': 'RTL FreeRTOS heap, bytes; instrumented build, dedicated LOG UART',
    'sample_count': len(samples),
    'before_first_tls_bytes': free('before_tls'),
    'tls_open_bytes': free('tls_open'),
    'first_tls_closed_bytes': free('first_tls_closed'),
    'warm_baseline_bytes': free('baseline'),
    'https_cycles': 100,
    'certificate_rejections': 30,
    'https_closed_heap_min_bytes': min(cycles),
    'https_closed_heap_max_bytes': max(cycles),
    'first_ten_median_bytes': statistics.median(cycles[:10]),
    'last_ten_median_bytes': statistics.median(cycles[-10:]),
    'settled_final_bytes': free('settled_130'),
    'post_warmup_loss_bytes': free('baseline') - free('settled_130'),
    'allowed_post_warmup_loss_bytes': 1024,
    'minimum_ever_free_bytes': min(s['minimum_ever_free_bytes'] for s in samples.values()),
    'gate_passed': 'PHASE5 RTL HEAP GATE PASSED' in text and
                   free('baseline') - free('settled_130') <= 1024 and 'Traceback' not in text,
    'samples': samples,
}
args.output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({k: v for k, v in result.items() if k != 'samples'}, indent=2))
if not result['gate_passed']:
    raise SystemExit('Runtime heap gate failed; inspect recorded measurements')
