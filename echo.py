import socket

s = socket.create_server(("localhost", 2323))

while True:
    s2 = s.accept()[0]
    while True:
        data = s2.recv(1024)
        if not data:
            break
            
        s2.send(data)