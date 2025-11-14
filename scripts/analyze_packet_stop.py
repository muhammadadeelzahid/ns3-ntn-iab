#!/usr/bin/env python3
"""
Analyze why packets stop being received at 1.0 seconds
"""

import os
import sys

def analyze_timing():
    """Analyze timing of packet reception"""
    
    print("=" * 80)
    print("PACKET RECEPTION TIMING ANALYSIS")
    print("=" * 80)
    print()
    
    # Read DashClientRx trace
    if os.path.exists("DashClientRx_UE_0.txt"):
        print("DASH CLIENT RX TRACE:")
        print("-" * 80)
        with open("DashClientRx_UE_0.txt", 'r') as f:
            lines = f.readlines()
            times = []
            for line in lines[2:]:  # Skip header
                if line.strip() and not line.startswith('#'):
                    parts = line.strip().split('\t')
                    if len(parts) >= 1:
                        time = float(parts[0])
                        times.append(time)
            
            if times:
                print(f"  First frame:     {times[0]:.6f} s")
                print(f"  Last frame:      {times[-1]:.6f} s")
                print(f"  Total frames:     {len(times)}")
                print(f"  Reception span:  {times[-1] - times[0]:.6f} s")
                
                # Check for gaps
                print(f"\n  Timing Analysis:")
                print(f"    Time to 1.0s:   {sum(1 for t in times if t < 1.0)} frames before 1.0s")
                print(f"    At 1.0s:        {sum(1 for t in times if 0.999 <= t <= 1.001)} frames")
                print(f"    After 1.0s:      {sum(1 for t in times if t > 1.001)} frames")
                
                # Check last few frames
                print(f"\n  Last 10 frames:")
                for i, t in enumerate(times[-10:], start=len(times)-9):
                    print(f"    Frame {i}: {t:.6f} s")
                
                # Check for gaps > 0.1s
                gaps = []
                for i in range(len(times)-1):
                    gap = times[i+1] - times[i]
                    if gap > 0.1:
                        gaps.append((times[i], times[i+1], gap))
                
                if gaps:
                    print(f"\n  ⚠ Gaps > 0.1s found:")
                    for start, end, gap in gaps:
                        print(f"    Gap from {start:.6f}s to {end:.6f}s: {gap:.3f}s")
                else:
                    print(f"\n  ✓ No significant gaps found")
    
    print()
    
    # Read QUIC Rx trace
    if os.path.exists("clientQUIC-rx-data2.txt"):
        print("QUIC CLIENT RX TRACE:")
        print("-" * 80)
        with open("clientQUIC-rx-data2.txt", 'r') as f:
            lines = f.readlines()
            times = []
            for line in lines:
                if line.strip() and not line.startswith('#'):
                    parts = line.strip().split('\t')
                    if len(parts) >= 1:
                        time = float(parts[0])
                        times.append(time)
            
            if times:
                print(f"  First packet:    {times[0]:.6f} s")
                print(f"  Last packet:     {times[-1]:.6f} s")
                print(f"  Total packets:   {len(times)}")
                
                # Check when packets stop
                print(f"\n  Packet Reception Analysis:")
                print(f"    Packets < 1.0s:  {sum(1 for t in times if t < 1.0)}")
                print(f"    Packets at 1.0s: {sum(1 for t in times if 0.999 <= t <= 1.001)}")
                print(f"    Packets > 1.0s:  {sum(1 for t in times if t > 1.001)}")
                
                # Find where DASH frames stop but QUIC continues
                last_dash_time = 0
                if os.path.exists("DashClientRx_UE_0.txt"):
                    with open("DashClientRx_UE_0.txt", 'r') as f:
                        dash_lines = f.readlines()
                        for line in dash_lines[2:]:
                            if line.strip() and not line.startswith('#'):
                                parts = line.strip().split('\t')
                                if len(parts) >= 1:
                                    last_dash_time = float(parts[0])
                
                if last_dash_time > 0:
                    quic_after_dash = [t for t in times if t > last_dash_time + 0.01]
                    print(f"\n  QUIC packets after last DASH frame ({last_dash_time:.6f}s):")
                    print(f"    Count:          {len(quic_after_dash)} packets")
                    if quic_after_dash:
                        print(f"    First:           {quic_after_dash[0]:.6f} s")
                        print(f"    Last:            {quic_after_dash[-1]:.6f} s")
                        print(f"    ⚠ QUIC is still receiving but DASH stopped processing!")
    
    print()
    
    # Read Client Tx (requests)
    if os.path.exists("DashClientTx_UE_0.txt"):
        print("DASH CLIENT TX (REQUESTS):")
        print("-" * 80)
        with open("DashClientTx_UE_0.txt", 'r') as f:
            lines = f.readlines()
            for line in lines[2:]:  # Skip header
                if line.strip() and not line.startswith('#'):
                    parts = line.strip().split('\t')
                    if len(parts) >= 4:
                        time = float(parts[0])
                        size = int(parts[1])
                        total = int(parts[2])
                        print(f"  Request {total}: {time:.6f} s, {size} bytes")
    
    print()
    
    # Analysis conclusion
    print("=" * 80)
    print("ROOT CAUSE ANALYSIS")
    print("=" * 80)
    print()
    print("Based on the traces:")
    print("1. QUIC is receiving packets until ~6.0s (clientApps.Stop time)")
    print("2. DASH frames stop being processed at ~1.03s")
    print("3. This suggests the MpegPlayer buffer became full")
    print()
    print("PROBABLE CAUSE:")
    print("- The player buffer (1MB) filled up with received frames")
    print("- When buffer is full, ReceiveFrame() returns false")
    print("- HttpParser stops processing packets (line 147 in http-parser.cc)")
    print("- Even though QUIC continues receiving, frames can't be processed")
    print()
    print("SOLUTION:")
    print("- The player needs to consume frames (PlayFrame) faster")
    print("- OR increase the buffer size")
    print("- OR reduce the bitrate to send smaller segments")
    print("- OR check if PlayFrame is actually running")

if __name__ == "__main__":
    analyze_timing()




















