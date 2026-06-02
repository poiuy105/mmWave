#ifndef PORTAL_HTML_CONTENT_H
#define PORTAL_HTML_CONTENT_H

static const char PORTAL_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>LD Radar 配置</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
  background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);
  min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}
.card{background:#fff;border-radius:16px;box-shadow:0 20px 60px rgba(0,0,0,0.3);
  max-width:500px;width:100%;padding:32px}
h1{text-align:center;color:#333;margin-bottom:8px;font-size:24px}
.subtitle{text-align:center;color:#888;margin-bottom:24px;font-size:14px}
.form-group{margin-bottom:16px}
label{display:block;font-weight:600;color:#555;margin-bottom:6px;font-size:14px}
input,select{width:100%;padding:10px 12px;border:2px solid #e0e0e0;border-radius:8px;
  font-size:15px;transition:border-color 0.3s;outline:none}
input:focus,select:focus{border-color:#667eea}
.btn{width:100%;padding:12px;border:none;border-radius:8px;font-size:16px;
  font-weight:600;cursor:pointer;transition:all 0.3s}
.btn-primary{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff}
.btn-primary:hover{transform:translateY(-1px);box-shadow:0 4px 15px rgba(102,126,234,0.4)}
.btn-scan{background:#f0f0f0;color:#333;margin-bottom:8px}
.btn-scan.scanning{animation:pulse 1.5s infinite}
.btn-reset{background:none;color:#e74c3c;margin-top:12px;font-size:14px}
.btn-reset:hover{text-decoration:underline}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.5}}
.status{text-align:center;padding:12px;border-radius:8px;margin-top:16px;
  font-size:14px;display:none}
.status.success{display:block;background:#d4edda;color:#155724}
.status.error{display:block;background:#f8d7da;color:#721c24}
.status.info{display:block;background:#d1ecf1;color:#0c5460}
.section-title{color:#667eea;font-size:16px;margin-top:20px;margin-bottom:12px;
  padding-bottom:6px;border-bottom:2px solid #f0f0f0}
.pw-toggle{position:absolute;right:12px;top:50%;transform:translateY(-50%);
  cursor:pointer;color:#888;font-size:13px}
.pw-wrap{position:relative}
.pw-wrap input{padding-right:40px}
</style>
</head>
<body>
<div class="card">
  <h1>📡 LD Radar 配置</h1>
  <p class="subtitle">配置 WiFi 和 MQTT 连接</p>

  <div class="section-title">WiFi 网络</div>
  <div class="form-group">
    <label>WiFi 网络</label>
    <button class="btn btn-scan" id="scanBtn" onclick="scanWiFi()">🔍 扫描网络</button>
    <select id="wifiSsid"><option value="">-- 手动输入 --</option></select>
  </div>
  <div class="form-group">
    <label>WiFi 密码</label>
    <div class="pw-wrap">
      <input type="password" id="wifiPass" placeholder="WiFi 密码">
      <span class="pw-toggle" onclick="togglePw('wifiPass',this)">显示</span>
    </div>
  </div>

  <div class="section-title">MQTT 服务器</div>
  <div class="form-group">
    <label>Broker 地址</label>
    <input type="text" id="mqttUri" placeholder="mqtt://broker.hivemq.com">
  </div>
  <div class="form-group">
    <label>端口</label>
    <input type="number" id="mqttPort" value="1883" min="1" max="65535">
  </div>
  <div class="form-group">
    <label>用户名（可选）</label>
    <input type="text" id="mqttUser" placeholder="MQTT 用户名">
  </div>
  <div class="form-group">
    <label>密码（可选）</label>
    <div class="pw-wrap">
      <input type="password" id="mqttPass" placeholder="MQTT 密码">
      <span class="pw-toggle" onclick="togglePw('mqttPass',this)">显示</span>
    </div>
  </div>

  <button class="btn btn-primary" id="saveBtn" onclick="saveConfig()">💾 保存配置</button>
  <div class="status" id="status"></div>
  <div style="text-align:center">
    <button class="btn btn-reset" onclick="resetConfig()">重置配置</button>
  </div>
</div>

<script>
function togglePw(id,el){
  const inp=document.getElementById(id);
  inp.type=inp.type==='password'?'text':'password';
  el.textContent=inp.type==='password'?'显示':'隐藏';
}

function showStatus(msg,type){
  const s=document.getElementById('status');
  s.textContent=msg;s.className='status '+type;
}

async function scanWiFi(){
  const btn=document.getElementById('scanBtn');
  btn.classList.add('scanning');btn.textContent='扫描中...';
  try{
    const r=await fetch('/api/scan');
    const list=await r.json();
    list.sort((a,b)=>b.rssi-a.rssi);
    const sel=document.getElementById('wifiSsid');
    sel.innerHTML='<option value="">-- 手动输入 --</option>';
    list.forEach(ap=>{
      const icon=ap.security==='SECURED'?'🔒':'📶';
      const opt=document.createElement('option');
      opt.value=ap.ssid;
      opt.textContent=icon+' '+ap.ssid+' ('+ap.rssi+'dBm)';
      sel.appendChild(opt);
    });
    showStatus('找到 '+list.length+' 个网络','info');
  }catch(e){showStatus('扫描失败: '+e.message,'error')}
  btn.classList.remove('scanning');btn.textContent='🔍 扫描网络';
}

async function loadConfig(){
  try{
    const r=await fetch('/api/config');
    const cfg=await r.json();
    if(cfg.wifi_ssid)document.getElementById('wifiSsid').value=cfg.wifi_ssid;
    if(cfg.wifi_password)document.getElementById('wifiPass').value=cfg.wifi_password;
    if(cfg.mqtt_uri)document.getElementById('mqttUri').value=cfg.mqtt_uri;
    if(cfg.mqtt_port)document.getElementById('mqttPort').value=cfg.mqtt_port;
    if(cfg.mqtt_username)document.getElementById('mqttUser').value=cfg.mqtt_username;
    if(cfg.mqtt_password)document.getElementById('mqttPass').value=cfg.mqtt_password;
  }catch(e){}
}

async function saveConfig(){
  const ssid=document.getElementById('wifiSsid').value.trim();
  if(!ssid){showStatus('请选择或输入 WiFi 名称','error');return}
  const uri=document.getElementById('mqttUri').value.trim();
  if(!uri){showStatus('请输入 MQTT Broker 地址','error');return}

  const btn=document.getElementById('saveBtn');
  btn.disabled=true;btn.textContent='保存中...';

  const body=JSON.stringify({
    wifi_ssid:ssid,
    wifi_password:document.getElementById('wifiPass').value,
    mqtt_uri:uri,
    mqtt_port:parseInt(document.getElementById('mqttPort').value)||1883,
    mqtt_username:document.getElementById('mqttUser').value,
    mqtt_password:document.getElementById('mqttPass').value
  });

  try{
    const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body});
    const res=await r.json();
    if(res.success){
      showStatus('✅ 配置保存成功！设备正在连接...','success');
      btn.textContent='已保存';
    }else{
      showStatus('保存失败: '+(res.message||'未知错误'),'error');
      btn.disabled=false;btn.textContent='💾 保存配置';
    }
  }catch(e){
    showStatus('保存失败: '+e.message,'error');
    btn.disabled=false;btn.textContent='💾 保存配置';
  }
}

function resetConfig(){
  if(confirm('确定要重置所有配置吗？')){
    fetch('/api/config',{method:'DELETE'}).then(()=>{
      document.getElementById('wifiSsid').value='';
      document.getElementById('wifiPass').value='';
      document.getElementById('mqttUri').value='';
      document.getElementById('mqttPort').value='1883';
      document.getElementById('mqttUser').value='';
      document.getElementById('mqttPass').value='';
      showStatus('配置已重置','info');
    });
  }
}

loadConfig();
setTimeout(scanWiFi,500);
</script>
</body>
</html>
)rawliteral";

#endif /* PORTAL_HTML_CONTENT_H */
