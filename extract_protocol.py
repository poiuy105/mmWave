import pdfplumber

with pdfplumber.open(r"e:\Espidf\mmWave\docs\HLK-LD2460串口协议V1.0.pdf") as pdf:
    for i, page in enumerate(pdf.pages[:10]):  # 只读前 10 页
        text = page.extract_text()
        if text:
            print(f"\n=== Page {i+1} ===\n", flush=True)
            # 只打印包含命令/配置/功能码的内容
            lines = text.split('\n')
            for line in lines:
                if any(kw in line for kw in ['命令', '功能码', '配置', '设置', 'FD FC', '上报', 'ACK', '0x']):
                    print(line, flush=True)