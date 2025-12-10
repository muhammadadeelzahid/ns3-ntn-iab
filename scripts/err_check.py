#!/usr/bin/env python3 
import os

def find_assertion_failures(directory='.'):
    """
    Scans files with .err or .out extensions in a given directory 
    for the string 'assert failed' and prints the filename and 
    the complete line containing the phrase.
    """
    
    # 1. Iterate through all files and directories in the specified path
    for filename in os.listdir(directory):
        # 2. Check if the file has a target extension
        if filename.endswith(('.err', '.out')):
            filepath = os.path.join(directory, filename)
            
            # Check if it's a file (and not a directory that happens to end with .err/.out)
            if os.path.isfile(filepath):
                # print(f"\n--- Checking **{filename}** ---")
                
                try:
                    # 3. Open the file and read line by line
                    with open(filepath, 'r') as file:
                        found_failure = False
                        
                        # Use enumerate to get the line number as well
                        for line_number, line in enumerate(file, 1):
                            # 4. Check for the target phrase (case-insensitive search)
                            if "assert failed" in line.lower():
                                found_failure = True
                                # 5. Output the result
                                print(f"  **FAILURE FOUND** in {filename} on Line {line_number}:")
                                # Strip leading/trailing whitespace for clean output
                                print(f"    {line.strip()}")                             
                except IOError as e:
                    print(f"  ERROR: Could not read file {filename}. {e}")
                except Exception as e:
                    print(f"  An unexpected error occurred with file {filename}. {e}")

# Run the function, searching in the current directory ('.')
if __name__ == "__main__":
    print("Starting scan for 'assert failed' in *.err and *.out files...")
    find_assertion_failures()
    print("\nScan complete.")