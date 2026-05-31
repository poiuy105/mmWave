from serial import Serial
import time, sys

ser = Serial('COM33', 115200, timeout=0.1, dsrdtr=False)
time.sleep(30)  # 等待测试完成

data = b""
deadline = time.time() + 35
while time.time() < deadline:
    if ser.in_waiting:
        data += ser.read(ser.in_waiting)
    time.sleep(0.1)
ser.close()

# 只打印测试相关的日志
lines = data.decode('utf-8', errors='replace').split('\n')
for line in lines:
    if 'RADAR_TEST' in line or 'Install mode' in line or 'Sensitivity' in line or 'Detection range' in line or 'TEST' in line or 'PASSED' in line or 'FAILED' in line:
        print(line, flush=True)