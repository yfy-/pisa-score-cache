#!/usr/bin/env python3

import argparse
import sys
import logging

def main():
    parser = argparse.ArgumentParser(
        description="Create score cache file from cache_queries results")
    parser.add_argument("-i", "--input", nargs="?", type=argparse.FileType("r"),
                        default=sys.stdin, help="Input results file")
    parser.add_argument("-o", "--output", nargs="?",
                        type=argparse.FileType("w"), default=sys.stdout,
                        help="Output cache file")
    parser.add_argument("-q", "--query-column", type=int, default=1,
                        help="Result file column index (starting from 0)"
                        " for query terms")
    parser.add_argument("-f", "--final-threshold", default=4, type=int,
                        help="Result file column index (starting from 0) for"
                        " final query score threshold")
    try:
        args = parser.parse_args()
        with args.input as istream:
            result_lines = istream.readlines()

        qi = args.query_column
        fi = args.final_threshold
        cache_lines = []
        for rl in result_lines:
            rl_tokens = rl.split(";")
            cache_lines.append(f"{rl_tokens[qi]};{rl_tokens[fi]}")

        with args.output as ostream:
            ostream.write("\n".join(cache_lines))

        return 0
    except Exception as ex:
        logging.exception(str(ex))
        return 1


if __name__ == "__main__":
    exit(main())
