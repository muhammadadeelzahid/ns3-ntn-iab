#!/usr/bin/env python3
"""
count_trace_delay.py

Counts the number of occurrences of the exact text "TraceDelay TX"
in a single file or recursively in all files under a directory.
"""

import os
import argparse

TARGET_TEXT = "TraceDelay TX"

def count_in_file(filepath):
    try:
        with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        return content.count(TARGET_TEXT)
    except Exception as e:
        print(f"⚠️ Could not read {filepath}: {e}")
        return 0

def main():
    parser = argparse.ArgumentParser(description="Count occurrences of 'TraceDelay TX'.")
    parser.add_argument("path", help="File or directory to scan")
    args = parser.parse_args()

    path = args.path
    total_count = 0

    if os.path.isfile(path):
        total_count = count_in_file(path)
        print(f"{path}: {total_count} occurrence(s)")
    else:
        for root, _, files in os.walk(path):
            for file in files:
                full_path = os.path.join(root, file)
                count = count_in_file(full_path)
                if count > 0:
                    print(f"{full_path}: {count}")
                total_count += count

    print(f"\nTotal 'TraceDelay TX' occurrences: {total_count}")

if __name__ == "__main__":
    main()
