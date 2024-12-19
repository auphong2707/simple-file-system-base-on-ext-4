import os

def check_content_of_drive(drive_path):
    if not os.path.exists(drive_path):
        print(f"Drive file {drive_path} does not exist.")
        return

    with open(drive_path, 'rb') as drive_file:
        content = drive_file.read()
        print(f"Drive file {drive_path} contains {len(content)} bytes.")
        unique_bytes = set(content)
        print(f"Drive file {drive_path} contains {len(unique_bytes)} unique bytes.")
        print("Unique bytes in the drive file:")
        for byte in unique_bytes:
            print(byte)

if __name__ == "__main__":
    drive_path = 'drive.bin'
    check_content_of_drive(drive_path)