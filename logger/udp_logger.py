import socket
import datetime
import sys

# Configuration
UDP_IP = "0.0.0.0"
UDP_PORT = 4444
def main():
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind((UDP_IP, UDP_PORT))
    except Exception as e:
        print(f"Error: Could not bind to {UDP_IP}:{UDP_PORT}. {e}")
        sys.exit(1)

    # Generate a unique log filename with startup timestamp
    timestamp_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file_name = f"field_test_logs_{timestamp_str}.txt"

    print(f"==================================================")
    print(f" UDP Log Receiver started on port {UDP_PORT}")
    print(f" Saving logs to: {log_file_name}")
    print(f" Press Ctrl+C to stop.")
    print(f"==================================================")

    # Open file in append mode with line buffering
    with open(log_file_name, "a", encoding="utf-8", buffering=1) as log_file:
        while True:
            try:
                data, addr = sock.recvfrom(4096)
                timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                
                # Decode message, replace invalid characters
                message = data.decode("utf-8", errors="replace").rstrip('\r\n')
                
                # Prepare log line with timestamp and sender IP
                log_line = f"[{timestamp}] [{addr[0]}] {message}"
                
                # Print to stdout and write to file
                print(log_line)
                log_file.write(log_line + "\n")
                
            except KeyboardInterrupt:
                print("\nStopping UDP Log Receiver. Goodbye!")
                break
            except Exception as e:
                print(f"Unexpected error: {e}")

if __name__ == "__main__":
    main()
