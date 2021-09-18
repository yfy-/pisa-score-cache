#!/usr/bin/env python3

import argparse
import multiprocessing
import subprocess
from functools import partial

def label(shard_id: int):
    label = str(shard_id)
    return "0" * (3 - len(label)) + label

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("cmd")
    parser.add_argument("n_shard", type=int)
    parser.add_argument("-j", "--jobs", type=int, default=1)
    parser.add_argument("-s", "--start", type=int, default=0)
    args = parser.parse_args()
    workload = [args.cmd.replace("{}", label(s)) for
                s in range(args.n_shard) if s >= args.start]
    partial_run = partial(subprocess.run, shell=True)
    with multiprocessing.Pool(args.jobs) as p:
        p.map(partial_run, workload)

    return 0

if __name__ == "__main__":
    exit(main())
