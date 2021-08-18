#! /usr/bin/env python3
from argparse import ArgumentParser
from collections import defaultdict
from IPython import embed
import itertools
import json
from enum import Enum
from math import sqrt
import pandas as pd
import numpy as np
from pathlib import Path
from pprint import pprint
import re

from Graph import Grapher
from IDXGrapher import IDXGrapher
from IDXDataObject import IDXDataObject

import pandas as pd
pd.set_option('display.float_format', lambda x: '%.3f' % x)
#pd.set_option('display.max_rows', None)

################################################################################

def add_args(parser):
    subparsers = parser.add_subparsers()

    # For processing!
    process_fn = lambda args: IDXDataObject(results_dir=args.input_dir
                                            ).save_to_file(Path(args.output_file))
    process = subparsers.add_parser('process',
                                    help='Process all results into a single summary.')
    process.add_argument('--input-dir', '-i', default='./benchout',
                         help='Where the raw results live.')
    process.add_argument('--output-file', '-o', default='report.yaml',
                         help='Where to output the report')
    process.set_defaults(fn=process_fn)

    # For playing around!
    # For processing!
    interact_fn = lambda args: IDXDataObject(file_path=Path(args.input_file)
                                            ).interact()
    interact = subparsers.add_parser('interact',
                                    help='Play around with the data in an interactive shell.')
    interact.add_argument('--input-file', '-i', default='./report.yaml',
                         help='Which report to use.')
    interact.set_defaults(fn=interact_fn)

    # For summaries!
    summary_fn = lambda args: IDXDataObject(file_path=Path(args.input_file)
            ).summary(args.baseline, args.output_file, args.prefix, args.final)
    summary = subparsers.add_parser('summary',
                                    help='Display relavant results')
    summary.add_argument('--input-file', '-i', default='report.yaml',
                         help='Where the aggregations live.')
    summary.add_argument('--output-file', '-o', default=None,
                         help='Dump summary to TeX file.')
    summary.add_argument('--baseline', '-b', default='extent_trees',
                         help='What indexing structure to use as the baseline.')
    summary.add_argument('--prefix', '-p', default='',
                         help='Prefix to label the latex commands with.')
    summary.add_argument('--final', '-f', action='store_true',
                         help='If dumping to TeX, remove the tentative tags.')
    summary.set_defaults(fn=summary_fn)

    # For better summaries!
    summarize_fn = lambda args: IDXDataObject(file_path=Path(args.input_file)
            ).summarize(args.function_name, args.output_file, args.final)
    summarize = subparsers.add_parser('summarize',
                                    help='Display relavant results')
    summarize.add_argument('--input-file', '-i', default='report.yaml',
                         help='Where the aggregations live.')
    summarize.add_argument('--output-file', '-o', default=None,
                         help='Dump summary to TeX file.')
    summarize.add_argument('--function-name', '-n', required=True,
                         help='What summary function to use.')
    summarize.add_argument('--final', '-f', action='store_true',
                         help='If dumping to TeX, remove the tentative tags.')
    summarize.set_defaults(fn=summarize_fn)

    # For graphing!
    graph = subparsers.add_parser('graph',
                                  help='Graph from report.yaml')
    IDXGrapher.add_parser_args(graph)

################################################################################

def main():
    parser = ArgumentParser(description='Process results from automate.py.')
    add_args(parser)

    args = parser.parse_args()
    args.fn(args)

if __name__ == '__main__':
    exit(main())
