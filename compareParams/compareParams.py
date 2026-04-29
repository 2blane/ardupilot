# Compares two parameter files and outputs the differences. Give the base parameter file first and then the expanded parameter file.
import re
import sys

def load_params(filename):
    params = set()
    with open(filename) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            # Split by tab, comma, or any whitespace
            parts = re.split(r'[\t, ]+', line)
            if len(parts) >= 2:
                param_name = parts[0].strip()
                params.add(param_name)
    return params

def main():
    if len(sys.argv) != 3:
        print("Usage: python compareParams.py <clean_file.param> <current_file.param>")
        sys.exit(1)

    clean_file = sys.argv[1]
    current_file = sys.argv[2]

    clean_params = load_params(clean_file)
    current_params = load_params(current_file)

    print(f"Loaded {len(clean_params)} parameters from {clean_file}.")
    print(f"Loaded {len(current_params)} parameters from {current_file}.")
    
    extra_params = sorted(current_params - clean_params)

    print(f"\nFound {len(extra_params)} extra parameters in {current_file} not in {clean_file}:\n")
    for param in extra_params:
        print(param)

if __name__ == "__main__":
    main()