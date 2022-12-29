#!/usr/bin/env python3

"""Removes training log queries from the test log."""

import argparse


def _rewrite_sorted(query_line: str) -> str:
    return " ".join(sorted(query_line.rstrip().split()))


def _main():
    parser = argparse.ArgumentParser()
    parser.add_argument("train_log")
    parser.add_argument("test_log")
    args = parser.parse_args()
    with open(args.train_log) as tls:
        train_lines = tls.readlines()

    unique_queries = set(_rewrite_sorted(tl) for tl in train_lines)
    with open(args.test_log) as tls:
        test_lines = tls.readlines()

    for tl in test_lines:
        if _rewrite_sorted(tl) not in unique_queries:
            print(tl, end="")

    return 0


if __name__ == "__main__":
    exit(_main())
