import sys
import base64

def base64_to_hex_string(b64_string):
    try:
        # Decode the Base64 string to raw bytes
        decoded_bytes = base64.b64decode(b64_string)
    except Exception as e:
        raise ValueError(f"Invalid Base64 input: {e}")
    
    # Check if the decoded result is exactly 32 bytes
    if len(decoded_bytes) != 32:
        raise ValueError("Decoded Base64 input must be exactly 32 bytes.")
    
    # Convert each byte to its hex representation
    hex_values = [f"0x{byte:02x}" for byte in decoded_bytes]
    
    # Join the formatted hex values with commas
    formatted_output = "{ " + ", ".join(hex_values) + " };"
    return formatted_output

if __name__ == "__main__":
    # Check if a Base64 string was provided in command line arguments
    if len(sys.argv) != 2:
        print("Usage: python script.py <base64-string>")
        sys.exit(1)
    
    b64_string = sys.argv[1]
    try:
        formatted_hex = base64_to_hex_string(b64_string)
        print(formatted_hex)
    except ValueError as e:
        print(e)
