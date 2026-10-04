import socket, struct, time
PATH = "/run/speecher-keywatchd/socket"
NAMES = {0: "accepted", 1: "bad version", 2: "key not permitted", 3: "at the watch limit", 4: "too many requests"}
def ask(version, key_id):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(PATH)
    s.sendall(struct.pack("BB", version, key_id))
    v, r = struct.unpack("BB", s.recv(2)); return s, v, NAMES[r]
held = []
for key_id, name in [(1, "ShiftLeft"), (10, "F13"), (5, "AltLeft"), (11, "F14")]:
    s, v, r = ask(2, key_id); held.append(s); print(f"v2 watch {name}: reply v{v}, {r}")
for s in held: s.close()
time.sleep(0.2)
s, v, r = ask(1, 10); print(f"v1 watch F13: reply v{v}, {r}")
s2, v, r = ask(3, 11); print(f"v3 watch F14: reply v{v}, {r}")
