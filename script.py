import os
import pandas as pd
import matplotlib
import matplotlib.pyplot as plt
#from adjustText import adjust_text

"""
NS3 Network Simulation Analysis Script

This script has been modified to calculate throughput for the entire simulation duration
instead of instantaneous throughput. The new approach:

1. **Duration-based Throughput Calculation**: 
   - Collects all packet data throughout the simulation
   - Calculates total bytes received for each user type (IAB/ENB)
   - Divides total bytes by total simulation duration
   - Formula: Throughput = (Total_Bytes * 8) / Total_Duration

2. **Per-User Throughput**: 
   - Calculates individual user throughput using the same duration-based method
   - Categorizes users by connection type (IAB/ENB)

3. **Sum Rate**: 
   - Adds up throughput from all users to get total network capacity
   - Maintains separation between IAB and ENB connected users

4. **Throughput Segregation**: 
   - Continues to separate throughput calculations for IAB vs ENB users
   - Provides both individual and aggregated metrics
"""
def replace_commas_with_spaces(file_path):
    """Replaces all commas in the file with spaces."""
    with open(file_path, 'r') as file:
        content = file.read()
    content = content.replace(',', ' ')
    with open(file_path, 'w') as file:
        file.write(content)

def parse_enbs(file_path):
    """Parses enbs.txt and returns a table of Node ID, Cell ID, and Node Type."""
    data = []

    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if line.startswith("#") or not line:
                continue

            parts = line.split()
            if len(parts) < 8:
                continue

            node_type = parts[0]
            node_id = int(parts[3])  # "Node ID:" is at index 3
            cell_id = int(parts[6])  # "Cell ID:" is at index 6

            # Normalize node type
            if node_type.lower() == "mmdev" or node_type.lower() == "mmwave":
                node_type = "enodeB"

            data.append({
                "Node ID": node_id,
                "Cell ID": cell_id,
                "Node Type": node_type
            })

    return pd.DataFrame(data)

def parse_ues(file_path):
    """Parses ues.txt and returns a table of UE Node ID, IMSI, and coordinates."""
    data = []

    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if line.startswith("#") or not line:
                continue

            parts = line.split()
            if len(parts) < 9:
                continue

            node_id = int(parts[2])  # "Node ID:" is at index 2
            imsi = int(parts[4])    # "IMSI:" is at index 4
            x, y, z = [float(coord.strip("()")) for coord in parts[6:9]]

            data.append({
                "UE Node ID": node_id,
                "IMSI": imsi,
                "X": x,
                "Y": y,
                "Z": z
            })

    return pd.DataFrame(data)

def parse_state_transition(file_path):
    """Parses StateTransitionTrace.txt and maintains a table of unique IMSI vs Cell ID.
    Keeps the entry with the highest index for duplicate IMSIs."""
    data = []

    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if not line:
                continue

            parts = line.split()
            if len(parts) < 10:
                continue
            try:
                imsi = int(parts[3])  # IMSI is at index 3
                cell_id = int(parts[7])  # CellId is at index 5
                data.append({
                    "IMSI": imsi,
                    "Cell ID": cell_id
                })
            except ValueError:
                print(f"Error parsing line parse_state_transition")
                continue

    # Convert to DataFrame and keep the last occurrence for duplicate IMSIs
    df = pd.DataFrame(data)
    df = df.drop_duplicates(subset=["IMSI"], keep="last")
    return df

def filter_state_transition(state_transition_table, ue_table):
    """Filters the state transition table to retain only entries with IMSIs present in the UE table."""
    filtered_table = state_transition_table[state_transition_table["IMSI"].isin(ue_table["IMSI"])]
    return filtered_table

def create_ue_type_table(filtered_state_transition, enb_table, ue_table):
    """Creates a table categorizing UE Node IDs into IAB or ENB based on the Cell ID."""
    # Merge filtered state transition with enb_table to get Node Type
    merged_table = filtered_state_transition.merge(enb_table, on="Cell ID", how="left")
    merged_table = merged_table.merge(ue_table, on="IMSI", how="left")

    # Categorize UE Node IDs
    iab_ues = merged_table[merged_table["Node Type"] == "IAB"]["UE Node ID"].dropna().tolist()
    enb_ues = merged_table[merged_table["Node Type"] != "IAB"]["UE Node ID"].dropna().tolist()

    # Create the new table
    result_table = pd.DataFrame({
        "Type": ["IAB", "ENB"],
        "UE Node IDs": [", ".join(map(str, iab_ues)) if iab_ues else "", ", ".join(map(str, enb_ues)) if enb_ues else ""]
    })

    return result_table

def parse_udp_server_rx(file_path, ue_type_table):
    """
    Parses UdpServerRx.txt and calculates throughput and packet count for IAB, ENB, and total UEs.
    
    NEW APPROACH: Duration-based throughput calculation instead of instantaneous throughput.
    - Collects all packets and their receive times
    - Calculates total simulation duration
    - Throughput = (Total_Bytes * 8) / Total_Duration
    - This gives the average throughput over the entire simulation period
    """
    # Initialize table
    results = pd.DataFrame({
        "Type": ["IAB", "ENB", "Total"],
        "Sum Throughput": [0.0, 0.0, 0.0],
        "Packet Count": [0, 0, 0],
        "const-packet-sum-throughput": [0,0,0]
    })
    global count
    # Extract UE Node IDs for IAB and ENB
    iab_ues = set(map(int, ue_type_table[ue_type_table["Type"] == "IAB"]["UE Node IDs"].iloc[0].split(", "))) if "IAB" in ue_type_table["Type"].values and ue_type_table["UE Node IDs"].iloc[0] else set()
    enb_ues = set(map(int, ue_type_table[ue_type_table["Type"] == "ENB"]["UE Node IDs"].iloc[0].split(", "))) if "ENB" in ue_type_table["Type"].values and ue_type_table["UE Node IDs"].iloc[1] else set()

    # Initialize data structures to collect packet information for duration-based calculation
    iab_packets = []
    enb_packets = []
    all_packets = []
    
    # Track time range for the entire simulation
    min_time = float('inf')
    max_time = float('-inf')
    
    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if not line:
                continue

            parts = line.split()
            try:
                packet_size = int(parts[0])
                # Parse receive time (assuming it's in the format like +200000000.0ns)
                rx_time_str = parts[5]  # Assuming Rxtime is at index 5
                rx_time_ns = float(rx_time_str.strip("+ns"))
                rx_time_sec = rx_time_ns / 1e9  # Convert nanoseconds to seconds
                node_id = int(parts[9])

                # Update time range
                min_time = min(min_time, rx_time_sec)
                max_time = max(max_time, rx_time_sec)

                # Store packet information for throughput calculation
                packet_info = {
                    'size_bytes': packet_size,
                    'rx_time': rx_time_sec,
                    'node_id': node_id
                }
                
                if node_id in iab_ues:
                    iab_packets.append(packet_info)
                elif node_id in enb_ues:
                    enb_packets.append(packet_info)
                
                all_packets.append(packet_info)

            except (ValueError, IndexError):
                print(f"Error parsing line parse_udp_server_rx")

    # Calculate total simulation duration
    total_duration = max_time - min_time
    
    if total_duration > 0:
        # Calculate throughput for IAB users (total bytes / total duration)
        if iab_packets:
            iab_total_bytes = sum(p['size_bytes'] for p in iab_packets)
            iab_throughput_bps = (iab_total_bytes * 8) / total_duration
            iab_throughput_mbps = iab_throughput_bps / 1e6
            results.loc[results["Type"] == "IAB", "Sum Throughput"] = iab_throughput_mbps
            results.loc[results["Type"] == "IAB", "Packet Count"] = len(iab_packets)
        
        # Calculate throughput for ENB users (total bytes / total duration)
        if enb_packets:
            enb_total_bytes = sum(p['size_bytes'] for p in enb_packets)
            enb_throughput_bps = (enb_total_bytes * 8) / total_duration
            enb_throughput_mbps = enb_throughput_bps / 1e6
            results.loc[results["Type"] == "ENB", "Sum Throughput"] = enb_throughput_bps / 1e6
            results.loc[results["Type"] == "ENB", "Packet Count"] = len(enb_packets)
        
        # Calculate total throughput (sum of all bytes / total duration)
        if all_packets:
            total_bytes = sum(p['size_bytes'] for p in all_packets)
            total_throughput_bps = (total_bytes * 8) / total_duration
            total_throughput_mbps = total_throughput_bps / 1e6
            results.loc[results["Type"] == "Total", "Sum Throughput"] = total_throughput_mbps
            results.loc[results["Type"] == "Total", "Packet Count"] = len(all_packets)


    # Calculate average throughput (same as sum throughput for duration-based calculation)
    results["Average Throughput"] = results["Sum Throughput"]
    results["Average Throughput"] = results["Average Throughput"].fillna(0)  # Handle division by zero
    
    # Update const-packet-sum-throughput for "IAB"
    results.loc[results["Type"] == "IAB", "const-packet-sum-throughput"] = (
        results.loc[results["Type"] == "IAB", "Average Throughput"] * 35000
    )

    # Update const-packet-sum-throughput for "ENB"
    results.loc[results["Type"] == "ENB", "const-packet-sum-throughput"] = (
        results.loc[results["Type"] == "ENB", "Average Throughput"] * 35000
    )

    # Update const-packet-sum-throughput for "Total"
    results.loc[results["Type"] == "Total", "const-packet-sum-throughput"] = (
        results.loc[results["Type"] == "IAB", "const-packet-sum-throughput"].sum()
        + results.loc[results["Type"] == "ENB", "const-packet-sum-throughput"].sum()
    )

    return results

def calculate_per_user_throughput(file_path, ue_type_table):
    """Calculates per-user throughput for the entire simulation duration."""
    # Extract UE Node IDs for IAB and ENB
    iab_ues = set(map(int, ue_type_table[ue_type_table["Type"] == "IAB"]["UE Node IDs"].iloc[0].split(", "))) if "IAB" in ue_type_table["Type"].values and ue_type_table["UE Node IDs"].iloc[0] else set()
    enb_ues = set(map(int, ue_type_table[ue_type_table["Type"] == "ENB"]["UE Node IDs"].iloc[0].split(", "))) if "ENB" in ue_type_table["Type"].values and ue_type_table["UE Node IDs"].iloc[1] else set()
    
    # Initialize per-user data collection
    user_packets = {}
    min_time = float('inf')
    max_time = float('-inf')
    
    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if not line:
                continue

            parts = line.split()
            try:
                packet_size = int(parts[0])
                rx_time_str = parts[5]  # Rxtime is at index 5
                rx_time_ns = float(rx_time_str.strip("+ns"))
                rx_time_sec = rx_time_ns / 1e9  # Convert nanoseconds to seconds
                node_id = int(parts[9])

                # Update time range
                min_time = min(min_time, rx_time_sec)
                max_time = max(max_time, rx_time_sec)

                # Initialize user if not exists
                if node_id not in user_packets:
                    user_packets[node_id] = {
                        'total_bytes': 0,
                        'packet_count': 0,
                        'connection_type': 'IAB' if node_id in iab_ues else 'ENB' if node_id in enb_ues else 'Unknown'
                    }
                
                # Add packet data
                user_packets[node_id]['total_bytes'] += packet_size
                user_packets[node_id]['packet_count'] += 1

            except (ValueError, IndexError):
                print(f"Error parsing line in calculate_per_user_throughput")

    # Calculate per-user throughput
    total_duration = max_time - min_time
    user_throughputs = {}
    
    if total_duration > 0:
        for node_id, data in user_packets.items():
            throughput_bps = (data['total_bytes'] * 8) / total_duration
            throughput_mbps = throughput_bps / 1e6
            
            user_throughputs[node_id] = {
                'throughput_bps': throughput_bps,
                'throughput_mbps': throughput_mbps,
                'total_bytes': data['total_bytes'],
                'packet_count': data['packet_count'],
                'connection_type': data['connection_type'],
                'duration': total_duration
            }
    
    return user_throughputs

def calculate_sum_rate(user_throughputs):
    """Calculates the sum rate (addition of all throughput of users)."""
    if not user_throughputs:
        return 0, 0
    
    total_throughput_bps = sum(data['throughput_bps'] for data in user_throughputs.values())
    total_throughput_mbps = sum(data['throughput_mbps'] for data in user_throughputs.values())
    
    return total_throughput_bps, total_throughput_mbps

def calculate_average_latency(udp_file, ue_type_table):
    """Calculates average latency for IAB, ENB, and total UEs by summing and dividing at the end."""
    latency_table = {
        "IAB": {"sum_latency": 0, "packet_count": 0, "average_latency": 0},
        "ENB": {"sum_latency": 0, "packet_count": 0, "average_latency": 0},
        "Total": {"sum_latency": 0, "packet_count": 0, "average_latency": 0}
    }

    # Ensure ue_type_table uses sets of integers for "IAB" and "ENB"
    iab_ues = set(map(int, ue_type_table[ue_type_table["Type"] == "IAB"]["UE Node IDs"].iloc[0].split(", "))) if "IAB" in ue_type_table["Type"].values and ue_type_table["UE Node IDs"].iloc[0] else set()
    enb_ues = set(map(int, ue_type_table[ue_type_table["Type"] == "ENB"]["UE Node IDs"].iloc[0].split(", "))) if "ENB" in ue_type_table["Type"].values and ue_type_table["UE Node IDs"].iloc[1] else set()

    with open(udp_file, 'r') as file:
        for line in file:
            line = line.strip()
            if not line:
                continue

            parts = line.split()
            try:
                delay_ns = float(parts[7].strip("+ns"))  # Delay in nanoseconds
                delay_s = delay_ns / 1e6  # Convert nanoseconds to milliseconds
                node_id = int(parts[9])
                if node_id in iab_ues:
                    latency_table["IAB"]["sum_latency"] += delay_s
                    
                    latency_table["IAB"]["packet_count"] += 1
                elif node_id in enb_ues:
                    latency_table["ENB"]["sum_latency"] += delay_s
                    latency_table["ENB"]["packet_count"] += 1

                # Update total latency and packet count
                latency_table["Total"]["sum_latency"] += delay_s
                latency_table["Total"]["packet_count"] += 1

            except (ValueError, IndexError):
                print(f"Error parsing line calculate_average_latency")

    # Calculate average latency
    for key in latency_table:
        if latency_table[key]["packet_count"] > 0:
            latency_table[key]["average_latency"] = latency_table[key]["sum_latency"] / latency_table[key]["packet_count"]
        else:
            latency_table[key]["average_latency"] = 0

    return latency_table

from matplotlib.cm import tab10

def calculate_distance(x1, y1, x2, y2):
    """Calculate Euclidean distance between two points."""
    return ((x2 - x1) ** 2 + (y2 - y1) ** 2) ** 0.5

def format_distance(distance):
    """Format distance in a readable way (meters or kilometers)."""
    if distance < 0:
        return "N/A"
    elif distance < 1000:
        return f"{distance:.1f}m"
    else:
        return f"{distance/1000:.2f}km"

def plot_coordinates_with_labels(ue_file, enb_file, state_transition_table, save_dir=None, filename='ue_enb_iab_coordinates.png', show_plot=True):
    """Plots coordinates for UEs and ENBs with labels, color-coded by Cell ID, and includes buildings."""
    ue_coords = []
    enb_coords = []
    iab_coords = []
    texts = []  # To store text objects for adjustText
    cell_ue_count = {}  # To count the number of UEs connected to each cell

    # Parse UE file for coordinates and IMSI, Cell ID
    ue_table = parse_ues(ue_file)
    ue_table = ue_table.merge(state_transition_table, on="IMSI", how="left")

    # Extract Cell IDs and assign colors
    cell_ids = ue_table["Cell ID"].unique()
    color_map = {cell_id: tab10(i % 10) for i, cell_id in enumerate(cell_ids)}

    for _, row in ue_table.iterrows():
        ue_coords.append((row["X"], row["Y"], row["IMSI"], row["Cell ID"], row["UE Node ID"]))
        # Count UEs connected to each Cell ID
        cell_ue_count[row["Cell ID"]] = cell_ue_count.get(row["Cell ID"], 0) + 1

    # Debug: Print UE information
    print(f"Found {len(ue_coords)} UEs connected to cell IDs: {list(cell_ue_count.keys())}")

    # Parse ENB file for coordinates and Cell ID
    with open(enb_file, 'r') as file:
        for line in file:
            line = line.strip()
            if line.startswith("#") or not line:
                continue

            parts = line.split()
            if len(parts) < 9:
                continue

            node_type = parts[0].lower()
            node_id = int(parts[3])  # "Node ID:" is at index 3
            x, y, z = [float(coord.strip("()")) for coord in parts[8:11]]
            cell_id = int(parts[6])  # "Cell ID:" is at index 6

            if node_type == "iab":
                iab_coords.append((x, y, cell_id, node_id))
            else:
                enb_coords.append((x, y, cell_id, node_id))

    # Create a mapping of Cell ID to base station coordinates for distance calculation
    cell_to_base_coords = {}
    
    # Store base station coordinates for distance calculation
    for x, y, cell_id, node_id in enb_coords:
        cell_to_base_coords[cell_id] = (x, y)
    
    for x, y, cell_id, node_id in iab_coords:
        cell_to_base_coords[cell_id] = (x, y)

    # Debug: Print base station information
    print(f"Found {len(cell_to_base_coords)} base stations with cell IDs: {list(cell_to_base_coords.keys())}")

    # Plot coordinates with labels
    plt.figure(figsize=(12, 8))

    # Add buildings
    buildings_file = 'buildings.txt'
    with open(buildings_file, 'r') as file:
        for line in file:
            line = line.strip()
            if line.startswith("set object"):
                parts = line.split()
                try:
                    x1, y1 = map(float, parts[5].split(","))
                    x2, y2 = map(float, parts[7].split(","))
                    plt.gca().add_patch(plt.Rectangle((x1, y1), x2 - x1, y2 - y1,
                                                      fill=None, edgecolor='black', linestyle='--'))
                except (ValueError, IndexError):
                    print(f"Error parsing building line: {line}")

    for x, y, imsi, cell_id, node_id in ue_coords:
        plt.scatter(x, y, c=[color_map[cell_id]] if cell_id in color_map else 'black', marker='o', s=80)        
        
        # Calculate distance to connected base station
        distance = 0
        if cell_id in cell_to_base_coords:
            base_x, base_y = cell_to_base_coords[cell_id]
            distance = calculate_distance(x, y, base_x, base_y)
        else:
            # If cell ID not found, show warning and set distance to -1
            print(f"Warning: Cell ID {cell_id} not found in base station coordinates for UE {node_id}")
            distance = -1
        
        text = plt.text(x, y, f"Node ID: {node_id}\nIMSI: {imsi}\nCell ID: {cell_id}\nDist: {format_distance(distance)}", fontsize=8)
        texts.append(text)

    for x, y, cell_id, node_id in enb_coords:
        plt.scatter(x, y, c=[color_map[cell_id]] if cell_id in color_map else 'black', marker='x', s=80)
        text = plt.text(x, y, f"Node ID: {node_id}\nCell ID: {cell_id}\nCoord: ({x:.1f}, {y:.1f})", fontsize=8)
        texts.append(text)

    for x, y, cell_id, node_id in iab_coords:
        plt.scatter(x, y, c=[color_map[cell_id]] if cell_id in color_map else 'black', marker='^', s=80)        
        text = plt.text(x, y, f"Node ID: {node_id}\nCell ID: {cell_id}\nCoord: ({x:.1f}, {y:.1f})", fontsize=8)
        texts.append(text)

    # Adjust text to avoid overlap
    #adjust_text(texts, arrowprops=dict(arrowstyle="->", color='gray', lw=0.5))

    # Simplified legend with unique labels
    plt.scatter([], [], c='black', marker='o', label='UE')
    plt.scatter([], [], c='black', marker='x', label='Donor Node')
    plt.scatter([], [], c='black', marker='^', label='IAB Node')

    plt.legend()

    plt.xlabel('X Coordinate (m)')
    plt.ylabel('Y Coordinate (m)')
    plt.title('UE and Donor/IAB Coordinates with Node IDs, Distances and Building Layout')
    plt.grid(True)
    # Save plot to disk as well
    script_dir = os.path.dirname(os.path.abspath(__file__)) if '__file__' in globals() else os.getcwd()
    output_dir = save_dir if save_dir else os.path.join(script_dir, 'plots')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, filename)
    plt.tight_layout()
    try:
        plt.savefig(output_path, dpi=300, bbox_inches='tight')
        print(f"Saved plot to: {output_path}", flush=True)
    except Exception as e:
        print(f"Failed to save plot to {output_path}: {e}", flush=True)

    # Show plot only if backend is interactive
    backend = matplotlib.get_backend().lower()
    if show_plot and ('agg' not in backend and 'pdf' not in backend and 'svg' not in backend):
        plt.show()
    else:
        print(f"Skipping display on non-interactive backend: {backend}", flush=True)
    plt.close()

def parse_rx_packet_trace_with_avg(file_path):
    """
    Parses RxPacketTraceENB.txt, retains UL/DL and SINR(dB) values,
    and calculates the average SINR for each direction.

    Args:
        file_path (str): Path to the RxPacketTraceENB.txt file.

    Returns:
        pd.DataFrame: A DataFrame containing the average SINR for UL and DL directions.
    """
    data = []

    with open(file_path, 'r') as file:
        for line in file:
            line = line.strip()
            if not line or line.startswith("frame"):  # Skip empty lines and headers
                continue
            
            parts = line.split()
            try:
                direction = parts[0]  # UL or DL
                sinr = float(parts[20])  # SINR(dB) is at index 10
                data.append({"Direction": direction, "SINR(dB)": sinr})
            except (IndexError, ValueError) as e:
                print(f"Error parsing line: {line} - {e}")

    # Convert to a DataFrame
    df = pd.DataFrame(data)

    # Calculate average SINR for UL and DL
    avg_sinr = (
        df.groupby("Direction", as_index=False)["SINR(dB)"]
        .mean()
        .rename(columns={"SINR(dB)": "Average SINR"})
    )

    # Ensure both UL and DL are represented in the output, even if missing
    directions = ["UL", "DL"]
    for direction in directions:
        if direction not in avg_sinr["Direction"].values:
            avg_sinr = pd.concat(
                [avg_sinr, pd.DataFrame({"Direction": [direction], "Average SINR": [0.0]})],
                ignore_index=True,
            )

    return avg_sinr

# Example usage
replace_commas_with_spaces('enbs.txt')
replace_commas_with_spaces('ues.txt')
replace_commas_with_spaces('StateTransitionTrace.txt')
replace_commas_with_spaces('UdpServerRx.txt')

enb_table = parse_enbs('enbs.txt')
ue_table = parse_ues('ues.txt')
state_transition_table = parse_state_transition('StateTransitionTrace.txt')

filtered_state_transition = filter_state_transition(state_transition_table, ue_table)
ue_type_table = create_ue_type_table(filtered_state_transition, enb_table, ue_table)

print("Filtered State Transition Table:")
print(filtered_state_transition)
pd.set_option('display.width', 120)  # Adjust display width
pd.set_option('display.max_columns', None)

print("UE Type Table:")
print(ue_type_table)
udp_results = parse_udp_server_rx('UdpServerRx.txt', ue_type_table)
print("UDP Server Results:")
print(udp_results)

# Calculate per-user throughput
user_throughputs = calculate_per_user_throughput('UdpServerRx.txt', ue_type_table)
print("\nPer-User Throughput:")
for node_id, data in sorted(user_throughputs.items()):
    print(f"Node {node_id} ({data['connection_type']}): {data['throughput_mbps']:.2f} Mbps")

# Calculate sum rate
sum_rate_bps, sum_rate_mbps = calculate_sum_rate(user_throughputs)
print(f"\nSum Rate (Total Throughput): {sum_rate_mbps:.2f} Mbps ({sum_rate_bps:,.0f} bps)")

latency_table = calculate_average_latency('UdpServerRx.txt', ue_type_table)
print("Latency Table:")
print(latency_table)

average_sinr_table = parse_rx_packet_trace_with_avg('RxPacketTrace.txt')
print(average_sinr_table)

plot_coordinates_with_labels('ues.txt', 'enbs.txt', state_transition_table)
