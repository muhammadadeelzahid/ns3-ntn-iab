#!/usr/bin/env python3 
import os
import re

TAIL_LINES = 100
READ_BLOCK_SIZE = 8192


def read_tail_lines(filepath, max_lines=TAIL_LINES):
    """
    Read the last `max_lines` lines from a file without scanning the full file.
    Returns a list of decoded lines (UTF-8 with replacement for invalid bytes).
    """
    with open(filepath, 'rb') as file:
        file.seek(0, os.SEEK_END)
        file_size = file.tell()
        if file_size == 0:
            return []

        buffer = b""
        lines = []
        pos = file_size

        while pos > 0 and len(lines) <= max_lines:
            read_size = min(READ_BLOCK_SIZE, pos)
            pos -= read_size
            file.seek(pos)
            chunk = file.read(read_size)
            buffer = chunk + buffer
            lines = buffer.splitlines()

        tail = lines[-max_lines:]
        return [line.decode('utf-8', errors='replace') for line in tail]

QUIC_FILENAME_RE = re.compile(r"^quic-cc-(\d+)_(\d+)\.(err|out)$")
TCP_FILENAME_RE = re.compile(r"^tcp-cc-(\d+)_(\d+)\.(err|out)$")

# (array_id_low, array_id_high, algorithm_name) - populated from Slurm scripts
QUIC_ALGOS = []
TCP_ALGOS = []


def parse_slurm_algos(slurm_path):
    """
    Parse a quic or tcp job parallel Slurm script. Returns [(low, high, algo_name), ...].
    Reads #SBATCH --array=1-N for max id and if/elif -le N + SHORT_NAME= for ranges.
    """
    if not os.path.isfile(slurm_path):
        return []
    with open(slurm_path, 'r') as f:
        content = f.read()
    # Max array id from e.g. #SBATCH --array=1-60
    array_match = re.search(r'#SBATCH\s+--array=1-(\d+)', content)
    max_id = int(array_match.group(1)) if array_match else 0
    # Upper bounds in order: -le 30, -le 60, ...
    bounds = [int(m) for m in re.findall(r'-le\s+(\d+)', content)]
    # Algorithm names in order: SHORT_NAME="NewReno", ... (one per if/elif/else block)
    names = re.findall(r'SHORT_NAME="(\w+)"', content)
    if not names:
        return []
    ranges = []
    low = 1
    for i, high in enumerate(bounds):
        if i < len(names) and high >= low:
            ranges.append((low, high, names[i]))
        low = high + 1
    # Final else block: from last bound+1 to max_id (e.g. BBR for QUIC, or when names > bounds)
    if low <= max_id and len(names) > len(bounds):
        ranges.append((low, max_id, names[-1]))
    elif low <= max_id and bounds:
        ranges.append((low, max_id, names[-1]))
    return ranges


def get_existing_array_ids(directory='.'):
    """Scan directory for quic-cc-* and tcp-cc-* .err/.out files; return (existing_quic, existing_tcp) sets of array_ids."""
    existing_quic = set()
    existing_tcp = set()
    for filename in os.listdir(directory):
        if not filename.endswith(('.err', '.out')):
            continue
        filepath = os.path.join(directory, filename)
        if not os.path.isfile(filepath):
            continue
        qmatch = QUIC_FILENAME_RE.match(filename)
        tmatch = TCP_FILENAME_RE.match(filename)
        if qmatch:
            existing_quic.add(int(qmatch.group(2)))
        elif tmatch:
            existing_tcp.add(int(tmatch.group(2)))
    return (existing_quic, existing_tcp)


def print_run_summary(corrupted_quic, corrupted_tcp, existing_quic, existing_tcp, suffix="to remove"):
    """Print remaining and removed counts per algorithm for QUIC and TCP. Totals are from runs present in the directory."""
    print("\n--- QUIC ---")
    for low, high, name in QUIC_ALGOS:
        total = sum(1 for aid in existing_quic if low <= aid <= high)
        count = sum(1 for aid in corrupted_quic if low <= aid <= high)
        remaining = total - count
        print(f"  {name}: {remaining}/{total} remaining, {count} {suffix}")
    print("--- TCP ---")
    for low, high, name in TCP_ALGOS:
        total = sum(1 for aid in existing_tcp if low <= aid <= high)
        count = sum(1 for aid in corrupted_tcp if low <= aid <= high)
        remaining = total - count
        print(f"  {name}: {remaining}/{total} remaining, {count} {suffix}")


def find_assertion_failures(directory='.', max_lines=TAIL_LINES):
    """
    Scans files with .err or .out extensions in a given directory 
    for corruption indicators:
    - 'assert failed'
    - 'Aborted' + '(core dumped)' (e.g. slurm_script: line 38: 1744897 Aborted (core dumped))
    Prints the filename and the complete line containing the phrase.
    Returns (corrupted_quic_array_ids, corrupted_tcp_array_ids).
    """
    corrupted_quic = set()
    corrupted_tcp = set()
    # 1. Iterate through all files and directories in the specified path
    for filename in os.listdir(directory):
        # 2. Check if the file has a target extension
        if filename.endswith(('.err', '.out')):
            filepath = os.path.join(directory, filename)
            
            # Check if it's a file (and not a directory that happens to end with .err/.out)
            if os.path.isfile(filepath):
                # print(f"\n--- Checking **{filename}** ---")
                shown_tail = False
                try:
                    # 3. Read only the last N lines for fast scanning
                    tail_lines = read_tail_lines(filepath, max_lines=max_lines)
                    for idx, line in enumerate(tail_lines, 1):
                        # 4. Check for corruption indicators
                        is_corrupted = False
                        if "assert failed" in line.lower():
                            is_corrupted = True
                        elif "Aborted" in line and "(core dumped)" in line:
                            is_corrupted = True

                        if is_corrupted:
                            # 5. Output the result
                            print(f"  **FAILURE FOUND** in {filename} in last {max_lines} lines (line {idx}):")
                            # Strip leading/trailing whitespace for clean output
                            print(f"    {line.strip()}")
                            # Additionally show the last 5 lines of the file (once per file)
                            if not shown_tail:
                                tail5 = tail_lines[-5:] if len(tail_lines) >= 5 else tail_lines
                                print("    Last 5 lines from this file:")
                                for tline in tail5:
                                    print(f"      {tline.rstrip()}")
                                shown_tail = True
                            qmatch = QUIC_FILENAME_RE.match(filename)
                            tmatch = TCP_FILENAME_RE.match(filename)
                            if qmatch:
                                array_id = int(qmatch.group(2))
                                corrupted_quic.add(array_id)
                            elif tmatch:
                                array_id = int(tmatch.group(2))
                                corrupted_tcp.add(array_id)
                except IOError as e:
                    print(f"  ERROR: Could not read file {filename}. {e}")
                except Exception as e:
                    print(f"  An unexpected error occurred with file {filename}. {e}")
    return (corrupted_quic, corrupted_tcp)


def _array_id_to_algo_run(array_id, algo_ranges):
    """Return (short_name, run_id) for array_id from algo_ranges [(low, high, name), ...], or (None, None)."""
    for low, high, name in algo_ranges:
        if low <= array_id <= high:
            return (name, array_id - low + 1)
    return (None, None)


def get_quic_run_dirs_for_array_id(array_id, repo_root, algo_ranges=None):
    """Return list of Quic_artifacts run dirs for this array_id (all relay_*)."""
    algo_ranges = algo_ranges or QUIC_ALGOS
    short_name, run_id = _array_id_to_algo_run(array_id, algo_ranges)
    if not short_name:
        return []
    quic_base = os.path.join(repo_root, "Quic_artifacts")
    if not os.path.isdir(quic_base):
        return []
    dirs = []
    for entry in os.listdir(quic_base):
        if entry.startswith("relay_"):
            run_dir = os.path.join(quic_base, entry, short_name, f"run_{run_id}")
            if os.path.isdir(run_dir):
                dirs.append(run_dir)
    return dirs


def get_tcp_run_dirs_for_array_id(array_id, repo_root, algo_ranges=None):
    """Return list of Tcp_artifacts run dirs for this array_id (all relay_*)."""
    algo_ranges = algo_ranges or TCP_ALGOS
    short_name, run_id = _array_id_to_algo_run(array_id, algo_ranges)
    if not short_name:
        return []
    tcp_base = os.path.join(repo_root, "Tcp_artifacts")
    if not os.path.isdir(tcp_base):
        return []
    dirs = []
    for entry in os.listdir(tcp_base):
        if entry.startswith("relay_"):
            run_dir = os.path.join(tcp_base, entry, short_name, f"run_{run_id}")
            if os.path.isdir(run_dir):
                dirs.append(run_dir)
    return dirs


def delete_corrupted_jobs(corrupted_quic, corrupted_tcp, directory='.'):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))

    for array_id in corrupted_quic:
        for filename in os.listdir(directory):
            match = QUIC_FILENAME_RE.match(filename)
            if match and int(match.group(2)) == array_id:
                filepath = os.path.join(directory, filename)
                if os.path.isfile(filepath):
                    os.remove(filepath)
        for run_dir in get_quic_run_dirs_for_array_id(array_id, repo_root):
            for entry in os.listdir(run_dir):
                if entry.endswith(".txt"):
                    txt_path = os.path.join(run_dir, entry)
                    if os.path.isfile(txt_path):
                        os.remove(txt_path)

    for array_id in corrupted_tcp:
        for filename in os.listdir(directory):
            match = TCP_FILENAME_RE.match(filename)
            if match and int(match.group(2)) == array_id:
                filepath = os.path.join(directory, filename)
                if os.path.isfile(filepath):
                    os.remove(filepath)
        for run_dir in get_tcp_run_dirs_for_array_id(array_id, repo_root):
            for entry in os.listdir(run_dir):
                if entry.endswith(".txt"):
                    txt_path = os.path.join(run_dir, entry)
                    if os.path.isfile(txt_path):
                        os.remove(txt_path)

# Run the function, searching in the current directory ('.')
if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, ".."))
    quic_slurm = os.path.join(repo_root, "quic_job_parallel.slurm")
    tcp_slurm = os.path.join(repo_root, "tcp_job_parallel.slurm")
    QUIC_ALGOS.clear()
    QUIC_ALGOS.extend(parse_slurm_algos(quic_slurm))
    TCP_ALGOS.clear()
    TCP_ALGOS.extend(parse_slurm_algos(tcp_slurm))
    if not QUIC_ALGOS:
        QUIC_ALGOS.extend([(1, 20, "NewReno"), (21, 40, "BBR")])  # fallback
    if not TCP_ALGOS:
        TCP_ALGOS.extend([(1, 20, "Cubic"), (21, 40, "NewReno"), (41, 60, "BBR")])  # fallback

    existing_quic, existing_tcp = get_existing_array_ids()
    print(f"Starting scan for corrupted jobs ('assert failed', 'Aborted (core dumped)') in *.err and *.out files (last {TAIL_LINES} lines only)...")
    corrupted_quic, corrupted_tcp = find_assertion_failures()
    if corrupted_quic or corrupted_tcp:
        print_run_summary(corrupted_quic, corrupted_tcp, existing_quic, existing_tcp, "to remove")
        parts = []
        if corrupted_quic:
            parts.append(f"{len(corrupted_quic)} QUIC job(s) {sorted(corrupted_quic)}")
        if corrupted_tcp:
            parts.append(f"{len(corrupted_tcp)} TCP job(s) {sorted(corrupted_tcp)}")
        prompt = (
            f"\nCorrupted jobs: {'; '.join(parts)}. "
            "Press Enter to delete their .err/.out and .txt logs, or type anything to cancel: "
        )
        response = input(prompt)
        if response == "":
            delete_corrupted_jobs(corrupted_quic, corrupted_tcp)
            print("Deleted corrupted .err/.out files and .txt logs from Quic_artifacts and Tcp_artifacts.")
        else:
            print("Deletion cancelled.")
        print_run_summary(corrupted_quic, corrupted_tcp, existing_quic, existing_tcp, "removed" if response == "" else "to remove")
    else:
        print("\nNo corrupted jobs found.")
        print_run_summary(set(), set(), existing_quic, existing_tcp, "removed")
    print("Scan complete.")