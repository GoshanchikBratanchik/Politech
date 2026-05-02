import socket


def run_server():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind(('0.0.0.0', 12345))
    server.listen(1)
    print("Server running")
    c, addr = server.accept()
    ccope = c.recv(4040).decode()
    print("Client is", ccope)
    data = c.recv(1024).decode()
    print("Client return", data)
    while True:
        message = input().lower().encode()
        c.send(message)
        data = c.recv(1024).decode().strip()
        print("->: ", data.encode())


if __name__ == "__main__":
    run_server()
