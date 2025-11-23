import socket
import threading
import logging
import os

logging.basicConfig(level=logging.INFO)

BUFFER_SIZE = 4096
PORT = 12345
SAVE_FOLDER = "received_files"

def create_server_socket(port):
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind(('', port))
    server_socket.listen(5)

    if not os.path.exists(SAVE_FOLDER):
        os.makedirs(SAVE_FOLDER)

    return server_socket

def recv_exact(sock, size):
    data = b""
    while len(data) < size:
        packet = sock.recv(size - len(data))
        if not packet:
            return None
        data += packet
    return data

def handle_client_connection(client_socket):
    try:
        filename_size = recv_exact(client_socket, 4)
        if not filename_size:
            return

        filename_length = int.from_bytes(filename_size, "big")
        filename = recv_exact(client_socket, filename_length).decode()

        file_size_data = recv_exact(client_socket, 8)
        if not file_size_data:
            return

        file_size = int.from_bytes(file_size_data, "big")
        file_path = os.path.join(SAVE_FOLDER, filename)

        received = 0
        with open(file_path, "wb") as f:
            while received < file_size:
                chunk = client_socket.recv(BUFFER_SIZE)
                if not chunk:
                    break
                f.write(chunk)
                received += len(chunk)

        client_socket.send(b"File received OK")

    except Exception as e:
        logging.error(f"{e}")

    finally:
        client_socket.close()

def start_server(port):
    server_socket = create_server_socket(port)
    while True:
        client_socket, addr = server_socket.accept()
        threading.Thread(target=handle_client_connection, args=(client_socket,), daemon=True).start()

if __name__ == "__main__":
    start_server(PORT)
