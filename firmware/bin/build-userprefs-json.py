import json
import subprocess
import re

def get_macros_from_header(header_file):
    # Run clang to preprocess the header file and capture the output
    result = subprocess.run(['clang', '-E', '-dM', header_file], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Clang preprocessing failed: {result.stderr}")

    # Extract macros from the output
    macros = {}
    macro_pattern = re.compile(r'#define\s+(\w+)\s+(.*)')
    for line in result.stdout.splitlines():
        match = macro_pattern.match(line) 
        if match and 'USERPREFS_' in line and '_USERPREFS_' not in line:
            macros[match.group(1)] = match.group(2)

    return macros

def write_macros_to_json(macros, output_file):
    with open(output_file, 'w') as f:
        json.dump(macros, f, indent=4)

def main():
    header_file = 'userPrefs.h'
    output_file = 'userPrefs.jsonc'
    # Uncomment all macros in the header file
    with open(header_file, 'r') as file:
        lines = file.readlines()

    uncommented_lines = []
    for line in lines:
        stripped_line = line.strip().replace('/*', '').replace('*/', '')
        if stripped_line.startswith('//') and 'USERPREFS_' in stripped_line:
            # Replace "//"
            stripped_line = stripped_line.replace('//', '')
        uncommented_lines.append(stripped_line + '\n')

    with open(header_file, 'w') as file:
        for line in uncommented_lines:
                file.write(line)
    macros = get_macros_from_header(header_file)
    write_macros_to_json(macros, output_file)
    print(f"Macros have been written to {output_file}")

if __name__ == "__main__":
    main()