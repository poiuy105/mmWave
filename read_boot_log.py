from serial import Serial
import time, sys, subprocess

# 先重启设备
ser = Serial('COM33', 115200, timeout=0.1, dsrdtr=False)
ser.setDTR(False)  # 触发重启
time.sleep(0.1)
ser.setDTR(True)
time.sleep(0.5)
ser.close()

# 重新打开串口
time.sleep(2)
ser = Serial('COM33', 115200, timeout=0.1, dsrdtr=False)

data = b""
deadline = time.time() + 45  # 等待 45 秒捕获测试日志
while time.time() < deadline:
    if ser.in_waiting:
        data += ser.read(ser.in_waiting)
    time.sleep(0.1)
ser.close()

sys.stdout.buffer.write(data)
sys.stdout.buffer.flush()