#!/usr/bin/env python3

import argparse
from typing import List
import numpy as np
from collections import defaultdict

class RunResult:
    def __init__(self, run_line=None, pivot=None):
        self.qid = None
        self.terms = []
        self.pivot_time = None
        if run_line is None:
            return

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

    def pivot_time_merge(self, other):
        if self.qid != other.qid:
            raise ValueError("run results are not comparable")

        merged = RunResult()
        merged.qid = self.qid
        merged.terms = list(self.terms)
        self.pivot_time = np.max(self.pivot_time, other.pivot_time)
        return merged


def read_results(path, pivot_func) -> List[RunResult]:
    with open(path, "r") as inps:
        run_lines = inps.readlines()

    return [RunResult(rl.rstrip(), pivot=pivot_func) for rl in run_lines[1:]]

def main():
    parser = argparse.ArgumentParser(
        description="Extract stats from cache_queries")
    parser.add_argument("-i", "--input", nargs="+",
                        help="Input: output of cache_queries")
    parser.add_argument("-p", "--pivot-run", choices=["min", "mean", "med"],
                        default="min")
    args = parser.parse_args()

    pivot_dict = {"min": np.min, "mean": np.mean, "med": np.median}
    pivot = pivot_dict[args.pivot_run]
    all_run_results = []
    for inp in args.input:
        run_results = read_results(inp, pivot)
        all_run_results.append(run_results)

    if len(all_run_results) > 1:
        qid_times = defaultdict(float)
        for run_results in all_run_results:
            for r in run_results:
                qid_times[r.qid] = max(qid_times[r.qid], r.pivot_time)

        pivot_times = list(qid_times.values())
        print("---------------")
        print("MERGED RESULTS")
        print("---------------")
        print("Mean={:.2f} ms".format(np.mean(pivot_times)))
        print("Median={:.2f} ms".format(np.median(pivot_times)))
        print("90th={:.2f} ms".format(np.percentile(pivot_times, 90)))
        print("95th={:.2f} ms".format(np.percentile(pivot_times, 95)))
        print("99th={:.2f} ms".format(np.percentile(pivot_times, 99)))


    for i, arr in enumerate(all_run_results):
        print("")
        print("------------")
        print(f"RESULT {i}")
        print("------------")
        pivot_times = [r.pivot_time for r in arr]

        print("Mean={:.2f} ms".format(np.mean(pivot_times)))
        print("Median={:.2f} ms".format(np.median(pivot_times)))
        print("90th={:.2f} ms".format(np.percentile(pivot_times, 90)))
        print("95th={:.2f} ms".format(np.percentile(pivot_times, 95)))
        print("99th={:.2f} ms".format(np.percentile(pivot_times, 99)))
        print("Mean Initial Threshold Ratio={:.2f}".format(
            np.mean([rr.init_threshold_ratio for rr in arr])))
        print("Mean k'th Threshold Ratio={:.2f}".format(
            np.mean([rr.kth_threshold_ratio for rr in arr])))

    return 0

if __name__ == "__main__":
    exit(main())
