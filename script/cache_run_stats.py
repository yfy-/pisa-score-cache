#!/usr/bin/env python3

import argparse
import numpy as np
import sys

class RunResult:
    def __init__(self, run_line, pivot):
        tokens = run_line.split("\t")
        self.qid = int(tokens[0])
        self.terms = [int(t) for t in tokens[1].split(",")]
        self.init_threshold = float(tokens[2])
        self.k_threshold = float(tokens[3])
        self.final_threshold = float(tokens[4])
        self.init_threshold_ratio = 0.0
        self.kth_threshold_ratio = 0.0
        if self.final_threshold:
            self.init_threshold_ratio = self.init_threshold /\
                self.final_threshold
            self.kth_threshold_ratio = self.k_threshold / self.final_threshold
        self.times = [float(tokens[i]) / 1000 for i in range(5, len(tokens))]
        self.pivot_time = pivot(self.times)

def main():
    parser = argparse.ArgumentParser(
        description="Extract stats from cache_queries")
    parser.add_argument("-i", "--input", nargs="?", default=sys.stdin,
                        type=argparse.FileType(),
                        help="Input: output of cache_queries")
    parser.add_argument("-o", "--output", nargs="?", default=sys.stdout,
                        type=argparse.FileType(mode="w"), help="Output file")
    parser.add_argument("-p", "--pivot-run", choices=["min", "mean", "med"],
                        default="min")
    args = parser.parse_args()

    pivot_dict = {"min": np.min, "mean": np.mean, "med": np.median}
    pivot = pivot_dict[args.pivot_run]
    with args.input as ist:
        run_lines = ist.readlines()

    run_results = [RunResult(rl.rstrip(), pivot) for rl in run_lines[1:]]
    pivot_times = [rr.pivot_time for rr in run_results]

    with args.output as os:
        os.write("Mean={:.2f} ms\n".format(np.mean(pivot_times)))
        os.write("Median={:.2f} ms\n".format(np.median(pivot_times)))
        os.write("90th={:.2f} ms\n".format(np.percentile(pivot_times, 90)))
        os.write("95th={:.2f} ms\n".format(np.percentile(pivot_times, 95)))
        os.write("99th={:.2f} ms\n".format(np.percentile(pivot_times, 99)))
        os.write("Mean Initial Threshold Ratio={:.2f}\n".format(
            np.mean([rr.init_threshold_ratio for rr in run_results])))
        os.write("Mean k'th Threshold Ratio={:.2f}\n".format(
            np.mean([rr.kth_threshold_ratio for rr in run_results])))

    return 0

if __name__ == "__main__":
    exit(main())
