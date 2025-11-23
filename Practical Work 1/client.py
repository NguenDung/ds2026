import socket
import logging
import os

logging.basicConfig(level=logging.INFO)

BUFFER_SIZE = 4096
PORT = 12345

def create_client_socket(server_address, port):
    try:
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.connect((server_address, port))
        logging.info(f"Connected to server at {server_address}:{port}")
        return client_socket
    except Exception as e:
        logging.error(f"Failed to connect to server: {e}")
        return None

def send_file(client_socket, file_path):
    if not os.path.isfile(file_path):
        print("File not found!")
        return

    filename = os.path.basename(file_path)
    file_size = os.path.getsize(file_path)

    try:
        logging.info(f"Sending file: {filename} ({file_size} bytes)")

        client_socket.send(len(filename).to_bytes(4, "big"))

        client_socket.send(filename.encode())

        client_socket.send(file_size.to_bytes(8, "big"))

        with open(file_path, "rb") as f:
            while True:
                chunk = f.read(BUFFER_SIZE)
                if not chunk:
                    break
                client_socket.sendall(chunk)

        logging.info("File sent. Waiting for server response...")

        response = client_socket.recv(BUFFER_SIZE).decode(errors="ignore")
        print("Server:", response)

    except Exception as e:
        logging.error(f"Error sending file: {e}")


if __name__ == "__main__":
    server_address = "localhost"

    try:
        while True:
            file_path = input("Enter file path to send (or 'exit'): ").strip()
            if file_path.lower() == "exit":
                break
            if not file_path:
                continue

            client_socket = create_client_socket(server_address, PORT)
            if client_socket is None:
                continue

            try:
                send_file(client_socket, file_path)
            finally:
                try:
                    client_socket.close()
                except:
                    pass
                logging.info("Connection closed for this file.")

    except KeyboardInterrupt:
        logging.info("Client shutting down.")
