"""
直接通过串口发送 LD2460 命令，观察雷达原始响应
"""
from serial import Serial
import time, sys

ser = Serial('COM33', 115200, timeout=0.1, dsrdtr=False)

# 先清空缓冲区
time.sleep(1)
while ser.in_waiting:
    ser.read(ser.in_waiting)

# 发送查询安装模式命令
# FD FC FB FA 0A 0C 00 01 04 03 02 01
cmd = bytes([0xFD, 0xFC, 0xFB, 0xFA, 0x0A, 0x0C, 0x00, 0x01, 0x04, 0x03, 0x02, 0x01])
print(f"Sending: {cmd.hex(' ')}", flush=True)
ser.write(cmd)

# 等待响应
time.sleep(2)
response = b""
deadline = time.time() + 3
while time.time() < deadline:
    if ser.in_waiting:
        response += ser.read(ser.in_waiting)
    time.sleep(0.05)

if response:
    print(f"Response ({len(response)} bytes): {response.hex(' ')}", flush=True)
else:
    print("No response received!", flush=True)

# 再试一个：查询固件版本
# FD FC FB FA 0B 0C 00 01 04 03 02 01
time.sleep(1)
while ser.in_waiting:
    ser.read(ser.in_waiting)

cmd2 = bytes([0xFD, 0xFC, 0xFB, 0xFA, 0x0B, 0x0C, 0x00, 0x01, 0x04, 0x03, 0x02, 0x01])
print(f"\nSending: {cmd2.hex(' ')}", flush=True)
ser.write(cmd2)

time.sleep(2)
response2 = b""
deadline = time.time() + 3
while time.time() < deadline:
    if ser.in_waiting:
        response2 += ser.read(ser.in_waiting)
    time.sleep(0.05)

if response2:
    print(f"Response ({len(response2)} bytes): {response2.hex(' ')}", flush=True)
else:
    print("No response received!", flush=True)

# 再试：关闭上报
# FD FC FB FA 06 0C 00 00 04 03 02 01
time.sleep(1)
while ser.in_waiting:
    ser.read(ser.in_waiting)

cmd3 = bytes([0xFD, 0xFC, 0xFB, 0xFA, 0x06, 0x0C, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01])
print(f"\nSending: {cmd3.hex(' ')}", flush=True)
ser.write(cmd3)

time.sleep(2)
response3 = b""
deadline = time.time() + 3
while time.time() < deadline:
    if ser.in_waiting:
        response3 += ser.read(ser.in_waiting)
    time.sleep(0.05)

if response3:
    print(f"Response ({len(response3)} bytes): {response3.hex(' ')}", flush=True)
else:
    print("No response received!", flush=True)

ser.close()
print("\nDone", flush=True)