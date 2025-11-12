# replace_text.py

def replace_in_file(input_file, string_a, string_b, output_file=None):
    """
    Replaces all occurrences of string_a with string_b in a text file.
    
    Parameters:
        input_file (str): Path to the input .txt file.
        string_a (str): The string to be replaced.
        string_b (str): The replacement string.
        output_file (str, optional): Path to save the modified file.
                                    If None, the input file is overwritten.
    """
    # Read file contents
    with open(input_file, 'r', encoding='utf-8') as file:
        content = file.read()
    
    # Replace all occurrences
    new_content = content.replace(string_a, string_b)
    
    # Write the modified content
    if output_file:
        with open(output_file, 'w', encoding='utf-8') as file:
            file.write(new_content)
        print(f"Replaced '{string_a}' with '{string_b}' and saved as '{output_file}'")
    else:
        with open(input_file, 'w', encoding='utf-8') as file:
            file.write(new_content)
        print(f"Replaced '{string_a}' with '{string_b}' in '{input_file}'")

# Example usage:
# replace_in_file('example.txt', 'old_string', 'new_string')
replace_in_file('zlogs.txt', '0x14f054200', 'CLIENT_SOCKET')
replace_in_file('zlogs.txt', '0x150089200', 'SERVER_SOCKET')