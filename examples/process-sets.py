#!/usr/bin/env python3.5

import argparse
import csv
import os
import random
import subprocess
import tempfile
import warnings

parser = argparse.ArgumentParser(
    description='Compare substrings of samples taken from two sets of files',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--data-root', type=str, default='.',
    help='Root path for example files')
parser.add_argument('--sample-size', type=float, default=512,
    help='Sample size in megabytes')
parser.add_argument('--seed', type=int, default=1,
    help='Random seed')
parser.add_argument('--filter', default=False, action='store_true',
    help='Filter redundant features (expensive)')
parser.add_argument('labels', metavar='labels', type=str, nargs=1,
    help='CSV file containg "label" and "file" columns')
args = parser.parse_args()

random.seed(args.seed)

pos_examples = []
neg_examples = []

for row in csv.DictReader(open(args.labels[0])):
  if row['label'] == '1':
    pos_examples.append(row['file'])
  else:
    neg_examples.append(row['file'])

random.shuffle(pos_examples)
random.shuffle(neg_examples)

pos_buffer = tempfile.NamedTemporaryFile(prefix='pos.')
neg_buffer = tempfile.NamedTemporaryFile(prefix='neg.')

seen = set()

for examples, buf in ((pos_examples, pos_buffer), (neg_examples, neg_buffer)):
  total_size = 0
  for example in examples:
    if total_size >= args.sample_size * 1024 * 1024:
      break

    try:
      data = open(os.path.join(args.data_root, example), 'rb').read()
    except FileNotFoundError:
      warnings.warn('Some input files are missing')
      continue

    data_hash = hash(data)
    if data_hash in seen:
      continue
    seen.add(data_hash)

    buf.write(data)
    buf.write(b'\0')
    total_size += len(data)


substring_frequencies_args = [
    '../substring-frequencies',
    '--document',
    '--skip-prefixes',
    ]

if not args.filter:
    substring_frequencies_args.append('--no-filter')

substring_frequencies_args += [
    '--',
    pos_buffer.name,
    neg_buffer.name
    ]

subprocess.Popen(substring_frequencies_args).wait()
