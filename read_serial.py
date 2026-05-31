from serial import Serial
import time, sys

ser = Serial('COM33', 115200, timeout=0.1, dsrdtr=False)
time.sleep(2)

data = b""
deadline = time.time() + 5
while time.time() < deadline:
    if ser.in_waiting:
        data += ser.read(ser.in_waiting)
    time.sleep(0.1)
ser.close()
sys.stdout.buffer.write(data)
sys.stdout.buffer.flush()