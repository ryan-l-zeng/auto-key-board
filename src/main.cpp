#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// 74HC595 引脚定义
#define SER      2
#define SRCLK    3
#define RCLK     10

// ====================== 自定义按键映射 ======================
const int KEY_HOME    = 42;   // Home键
const int KEY_SHIFT   = 58;   // Shift按键
const int KEY_ENTER   = 59;   // 回车按键
const int KEY_CTRL    = 60;   // Ctrl按键

// 函数原型声明
int getKeyChannel(char c);
bool needShift(char c, int &outCh);

// 【新增】网页复制缓存：保存文本框的内容
String copyBuffer = "";

// ====================================================================================
// WiFi 热点配置
WebServer server(80);
const char* AP_SSID = "ESP32-KEYBOARD";
const char* AP_PWD  = "12345678";

// 全局状态
String connectedWiFiName = "未连接";
IPAddress localIP;
Preferences prefs;

// ======================== 速度配置 ========================
int keyPressMs = 40;
int keyDelayMs = 60;
bool humanMode = false;
int currentMode = 1;

String getSpeedName() {
  switch(currentMode){
    case 0: return "慢速";
    case 1: return "中速";
    case 2: return "快速";
    case 3: return "拟人速";
    default: return "中速";
  }
}

void setSpeedSlow()  { keyPressMs = 60; keyDelayMs = 120; humanMode = false; currentMode = 0; }
void setSpeedMid()   { keyPressMs = 40; keyDelayMs = 60;  humanMode = false; currentMode = 1; }
void setSpeedFast()  { keyPressMs = 25; keyDelayMs = 30;  humanMode = false; currentMode = 2; }
void setSpeedHuman() { humanMode = true; currentMode = 3; }

// ====================================================================
bool connectToSavedWiFi() {
  Serial.println("[INFO] 尝试读取NVS保存的WiFi信息");
  prefs.begin("wifi", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPWD = prefs.getString("pwd", "");
  prefs.end();
  if (savedSSID.isEmpty()){
    Serial.println("[INFO] NVS内无保存WiFi");
    return false;
  }
  Serial.printf("[INFO] 读取到SSID:%s，开始连接...\n", savedSSID.c_str());
  WiFi.begin(savedSSID.c_str(), savedPWD.c_str());
  for (int i = 0; i < 20; i++) {
    delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[SUCCESS] STA连接成功，IP:");
      Serial.println(WiFi.localIP().toString());
      return true;
    }
  }
  Serial.println("[WARN] STA连接超时");
  return false;
}

void wifiMonitor() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();

  if (WiFi.status() == WL_CONNECTED) {
    connectedWiFiName = WiFi.SSID();
    localIP = WiFi.localIP();
    prefs.begin("wifi", false);
    prefs.putString("ssid", WiFi.SSID());
    prefs.putString("pwd", WiFi.psk());
    prefs.end();
  } else {
    connectedWiFiName = "未连接";
    localIP = IPAddress(0, 0, 0, 0);
  }
}

void send64bit(uint64_t data) {
  digitalWrite(RCLK, LOW);
  shiftOut(SER, SRCLK, MSBFIRST, (data >> 56) & 0xFF);
  shiftOut(SER, SRCLK, MSBFIRST, (data >> 48) & 0xFF);
  shiftOut(SER, SRCLK, MSBFIRST, (data >> 40) & 0xFF);
  shiftOut(SER, SRCLK, MSBFIRST, (data >> 32) & 0xFF);
  shiftOut(SER, SRCLK, MSBFIRST, (data >> 24) & 0xFF);
  shiftOut(SER, SRCLK, MSBFIRST, (data >> 16) & 0xFF);
  shiftOut(SER, SRCLK, MSBFIRST, (data >> 8)  & 0xFF);
  shiftOut(SER, SRCLK, MSBFIRST, data & 0xFF);
  digitalWrite(RCLK, HIGH);
}

void allKeysOff() {
  send64bit(0);
}

void keyPress(int ch) {
  if (ch < 0 || ch > 63) return;
  allKeysOff();
  delay(10);
  uint64_t mask = (uint64_t)1 << ch;
  send64bit(mask);
  humanMode ? delay(random(50,100)) : delay(keyPressMs);
  allKeysOff();
  humanMode ? delay(random(80,250)) : delay(keyDelayMs);
}

void shiftKeyPress(int ch) {
  if (ch < 0 || ch > 63) return;
  allKeysOff();
  delay(10);
  uint64_t shiftMask = (uint64_t)1 << KEY_SHIFT;
  send64bit(shiftMask);
  delay(15);
  uint64_t keyMask = shiftMask | ((uint64_t)1 << ch);
  send64bit(keyMask);
  humanMode ? delay(random(50,100)) : delay(keyPressMs);
  send64bit((uint64_t)1 << ch);
  delay(15);
  allKeysOff();
  delay(10);
  humanMode ? delay(random(100,250)) : delay(keyDelayMs);
}

// 底层硬件Ctrl组合函数保留（可代码内部调用，网页不再使用）
void ctrlKeyPress(int ch)
{
  if (ch <0 || ch>63) return;
  allKeysOff();
  delay(10);
  uint64_t ctrlMask = (uint64_t)1 << KEY_CTRL;
  send64bit(ctrlMask);
  delay(15);
  uint64_t keyMask = ctrlMask | ((uint64_t)1 << ch);
  send64bit(keyMask);
  humanMode ? delay(random(50,100)) : delay(keyPressMs);
  send64bit((uint64_t)1 << ch);
  delay(15);
  allKeysOff();
  humanMode ? delay(random(100,250)) : delay(keyDelayMs);
}
void ctrlC()  { int ch_c = getKeyChannel('c'); ctrlKeyPress(ch_c); }
void ctrlV()  { int ch_v = getKeyChannel('v'); ctrlKeyPress(ch_v); }

bool needShift(char c, int &outCh) {
  switch (c) {
    case '~':  outCh = 0;  return true;
    case '!':  outCh = 1;  return true;
    case '@':  outCh = 2;  return true;
    case '#':  outCh = 3;  return true;
    case '$':  outCh = 4;  return true;
    case '%':  outCh = 5;  return true;
    case '^':  outCh = 6;  return true;
    case '&':  outCh = 7;  return true;
    case '*':  outCh = 8;  return true;
    case '(':  outCh = 9;  return true;
    case ')':  outCh = 10; return true;
    case '_':  outCh = 11; return true;
    case '+':  outCh = 12; return true;
    case '{':  outCh = 25; return true;
    case '}':  outCh = 26; return true;
    case '|':  outCh = 27; return true;
    case ':':  outCh = 38; return true;
    case '"':  outCh = 39; return true;
    case '<':  outCh = 50; return true;
    case '>':  outCh = 49; return true;
    case '?':  outCh = 48; return true;
    default:   return false;
  }
}

int getKeyChannel(char c) {
  switch (c) {
    case '`':  return 0;
    case '1':  return 1; case '2': return 2; case '3': return 3; case '4': return 4;
    case '5':  return 5; case '6': return 6; case '7': return 7; case '8': return 8;
    case '9':  return 9; case '0': return 10; case '-': return 11; case '=': return 12;
    case 8:    return 13;
    case '\t': return 14;
    case 'q':  return 15; case 'w': return 16; case 'e': return 17; case 'r': return 18;
    case 't':  return 19; case 'y': return 20; case 'u': return 21; case 'i': return 22;
    case 'o':  return 23; case 'p': return 24; case '[': return 25; case ']': return 26;
    case '\\': return 27;
    case 'a':  return 29; case 's': return 30; case 'd': return 31; case 'f': return 32;
    case 'g':  return 33; case 'h': return 34; case 'j': return 35; case 'k': return 36;
    case 'l':  return 37; case ';': return 38; case '\'': return 39;
    case '\n': return KEY_ENTER;
    case 'z':  return 57; case 'x': return 56; case 'c': return 55; case 'v': return 54;
    case 'b':  return 53; case 'n': return 52; case 'm': return 51; case ',': return 50;
    case '.':  return 49; case '/': return 48; case ' ': return 47;
    default:   return -1;
  }
}

void typeText(String s) {
  Serial.printf("[INFO] 输出文本，长度:%d\n", s.length());
  for (int i = 0; i < s.length(); i++) {
    char c = s[i];
    int ch;
    if (needShift(c, ch)) {
      shiftKeyPress(ch);
      continue;
    }
    if (c >= 'A' && c <= 'Z') {
      ch = getKeyChannel(c + 32);
      shiftKeyPress(ch);
      continue;
    }
    ch = getKeyChannel(c);
    if (ch >= 0) {
      keyPress(ch);
      if(c == '\n'){
        delay(80);
        keyPress(KEY_HOME);
      }
    }
  }
  Serial.println("[INFO] 文本输出完成");
}

// ===================== 网页主页 =====================
void sendHomePage() {
  String apIP = WiFi.softAPIP().toString();
  String staIP = (WiFi.status() == WL_CONNECTED)? localIP.toString() : "0.0.0.0";
  String speedName = getSpeedName();
  String html = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>键盘控制器</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial;margin:15px;background:#f7f8fa}
.container{max-width:800px;margin:auto;background:#fff;padding:20px;border-radius:12px}
.status{background:#eef;padding:12px;border-radius:8px;margin-bottom:15px}
.accordion{margin-bottom:15px}
.accordion-header{width:100%;padding:10px;background:#007bff;color:white;border:none;border-radius:8px;text-align:left;cursor:pointer;font-size:15px}
.accordion-content{max-height:0;overflow:hidden;transition:max-height 0.25s ease;background:#fafafa;border-radius:8px;padding:0 12px;margin-top:8px}
.accordion-content.open{max-height:340px;padding:12px;overflow-y:auto}
.btn-refresh{width:100%;padding:10px;background:#28a745;color:white;border:none;border-radius:8px;cursor:pointer;font-size:15px;margin-bottom:10px}
.wifi-item{padding:10px;border:1px solid #ddd;border-radius:8px;margin:6px 0;display:flex;justify-content:space-between;align-items:center}
button{border:none;border-radius:8px;color:white;cursor:pointer}
.btn-conn{padding:6px 12px;background:#007bff}
h4{margin:8px 0 10px 0;color:#333;font-size:15px}
textarea{width:100%;height:220px;padding:10px;border-radius:8px;border:1px solid #ddd;margin:10px 0}
.speed-btn{
  padding:12px;
  text-align:left;
  font-size:14px;
  background:#4da6ff;
  border:none;
  color:white;
  width:100%;
  margin-bottom:6px;
}
.speed-btn.active{
  background:#007bff;
  font-weight:bold;
}
.btn-row{display:flex;gap:10px;margin-top:10px;flex-wrap:wrap;}
</style>
</head>
<body>
<div class="container">
<h2>ESP32 键盘控制器</h2>
<div class="status">
<strong>AP 热点：)HTML" + String(AP_SSID) + R"HTML(</strong><br>
<strong>STA 连接：)HTML" + connectedWiFiName + R"HTML(</strong><br>
<strong>IP 地址：)HTML" + staIP + R"HTML(</strong>
</div>
<div class="accordion">
  <button class="accordion-header" onclick="toggleSpeed()">⚡ 打字速度 - 当前：)HTML"+speedName+R"HTML(</button>
  <div class="accordion-content" id="speedPanel">
    <button class="speed-btn )HTML"+(currentMode==0?"active":"")+R"HTML(" onclick="location.href='/slow'">🐢 慢速 (约 180 字符/分钟)</button>
    <button class="speed-btn )HTML"+(currentMode==1?"active":"")+R"HTML(" onclick="location.href='/mid'">⚡ 中速 (约 360 字符/分钟)</button>
    <button class="speed-btn )HTML"+(currentMode==2?"active":"")+R"HTML(" onclick="location.href='/fast'">🚀 快速 (约 600 字符/分钟)</button>
    <button class="speed-btn )HTML"+(currentMode==3?"active":"")+R"HTML(" onclick="location.href='/human'">👤 拟人速 (约 120~200 字符/分钟)</button>
  </div>
</div>
<div class="accordion">
<button class="accordion-header" onclick="toggleAccordion()">🔗 切换WiFi</button>
<div class="accordion-content" id="wifiPanel">
  <button class="btn-refresh" onclick="location.href='/scan'">🔄 刷新WiFi</button>
  <h4>附近 WiFi</h4>
  <div class="wifi-box">
    <div class='wifi-item'>点击【刷新WiFi】扫描网络</div>
  </div>
</div>
</div>
<form action="/run" method="post">
<textarea name="content" placeholder="在这里输入要自动打的文字..."></textarea>
<div class="btn-row">
<button type="submit" style="padding:10px 18px;background:#007bff">✅ 开始输入</button>
<button type="submit" formaction="/copybuf" style="padding:10px 18px;background:#28a745">📋复制框内文本到ESP缓存</button>
<button type="button" onclick="location.href='/pastebuf'" style="padding:10px 18px;background:#fd7e14">📄粘贴缓存（输出打字）</button>
</div>
</form>
</div>
<script>
function toggleAccordion(){
  let p = document.getElementById("wifiPanel");
  p.classList.toggle("open");
}
function toggleSpeed(){
  let p = document.getElementById("speedPanel");
  p.classList.toggle("open");
}
</script>
</body></html>
)HTML";
  server.send(200, "text/html", html);
}

void sendScanPage() {
  bool isStaOnline = (WiFi.status() == WL_CONNECTED);
  String staIP = isStaOnline? localIP.toString() : "0.0.0.0";
  String html = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial;margin:15px;background:#f7f8fa}
.container{max-width:800px;margin:auto;background:#fff;padding:20px;border-radius:12px}
.status{background:#eef;padding:12px;border-radius:8px;margin-bottom:15px}
.accordion{margin-bottom:15px}
.accordion-header{width:100%;padding:10px;background:#007bff;color:white;border:none;border-radius:8px;text-align:left;cursor:pointer;font-size:15px}
.accordion-content{max-height:340px;padding:12px;overflow-y:auto;background:#fafafa;border-radius:8px;margin-top:8px}
.btn-refresh{width:100%;padding:10px;background:#28a745;color:white;border:none;border-radius:8px;cursor:pointer;font-size:15px;margin-bottom:10px}
.wifi-item{padding:10px;border:1px solid #ddd;border-radius:8px;margin:6px 0;display:flex;justify-content:space-between;align-items:center}
button{border:none;border-radius:8px;color:white;cursor:pointer}
.btn-conn{padding:6px 12px;background:#007bff}
h4{margin:8px 0 10px 0;color:#333;font-size:15px}
textarea{width:100%;height:220px;padding:10px;border-radius:8px;border:1px solid #ddd;margin:10px 0}
.speed-btn{background:#4da6ff;padding:12px;text-align:left;font-size:14px;border:none;color:white;width:100%;margin-bottom:6px}
.speed-btn.active{background:#007bff;font-weight:bold}
.btn-row{display:flex;gap:10px;margin-top:10px;flex-wrap:wrap;}
</style>
</head>
<body>
<div class="container">
<h2>ESP32 键盘控制器</h2>
<div class="status">
<strong>AP 热点：)HTML" + String(AP_SSID) + R"HTML(</strong><br>
<strong>STA 连接：)HTML" + connectedWiFiName + R"HTML(</strong><br>
<strong>IP 地址：)HTML" + staIP + R"HTML(</strong>
</div>
<div class="accordion">
<button class="accordion-header" onclick="history.back()">🔗 切换WiFi</button>
<div class="accordion-content open">
  <button class="btn-refresh" onclick="location.href='/scan'">🔄 刷新WiFi</button>
  <h4>附近 WiFi</h4>
)HTML";
  WiFi.scanDelete();
  int currentChannel = WiFi.channel();
  int n = WiFi.scanNetworks(false, false, currentChannel, 120);
  if(n == WIFI_SCAN_FAILED) n = 0;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      html += "<div class='wifi-item'><span>📶 " + ssid + "</span><form action='/go' method='post' style='margin:0'><input type='hidden' name='ssid' value='" + ssid + "'><button type='submit' class='btn-conn'>连接</button></form></div>";
    }
  } else {
    html += "<div class='wifi-item'>未搜索到 WiFi</div>";
  }
  html += R"HTML(
  </div>
</div>
</div>
<form action="/run" method="post">
<textarea name="content" placeholder="在这里输入要自动打的文字..."></textarea>
<div class="btn-row">
<button type="submit" style="padding:10px 18px;background:#007bff">✅ 开始输入</button>
<button type="submit" formaction="/copybuf" style="padding:10px 18px;background:#28a745">📋复制框内文本到ESP缓存</button>
<button type="button" onclick="location.href='/pastebuf'" style="padding:10px 18px;background:#fd7e14">📄粘贴缓存（输出打字）</button>
</div>
</form>
</div>
</body></html>
)HTML";
  server.send(200, "text/html", html);
}

void sendGoPage() {
  String ssid = server.arg("ssid");
  String html = R"HTML(
<!DOCTYPE html>
<meta charset="utf-8">
<style>
body{background:#f7f8fa}
.box{max-width:400px;margin:50px auto;background:#fff;padding:24px;border-radius:12px}
input{width:100%;padding:10px;margin:10px 0;border-radius:8px;border:1px solid #ddd}
button{width:100%;padding:10px;background:#007bff;color:white;border:none;border-radius:8px}
</style>
<div class="box">
<h3>)HTML" + ssid + R"HTML(</h3>
<form action="/conn" method="post">
<input type='hidden' name='ssid' value=")HTML" + ssid + R"HTML(">
<input type="password" name="pwd" placeholder="输入WiFi密码" required>
<button type="submit">连接</button>
</form>
</div>
)HTML";
  server.send(200, "text/html", html);
}

void handleConn() {
  String ssid = server.arg("ssid");
  String pwd = server.arg("pwd");
  Serial.printf("[INFO] Web请求连接WiFi: %s\n", ssid.c_str());
  WiFi.disconnect(false);
  delay(200);
  WiFi.begin(ssid.c_str(), pwd.c_str());
  connectedWiFiName = "连接中...";
  for (int i = 0; i < 10; i++) {
    delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      connectedWiFiName = WiFi.SSID();
      localIP = WiFi.localIP();
      Serial.print("[SUCCESS] STA网页连接成功 IP:");
      Serial.println(localIP.toString());
      break;
    }
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleRun() {
  Serial.println("[INFO] Web收到执行输入指令");
  typeText(server.arg("content"));
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

// ========== 新接口：复制文本框内容到ESP内存缓存 ==========
void handleCopyBuf(){
  copyBuffer = server.arg("content");
  Serial.printf("[INFO] 已缓存网页文本，长度=%d\n", copyBuffer.length());
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}
// ========== 新接口：把缓存内容输出打字 ==========
void handlePasteBuf(){
  Serial.println("[INFO] 执行粘贴缓存输出");
  typeText(copyBuffer);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleSlow()  { setSpeedSlow();  server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
void handleMid()   { setSpeedMid();   server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
void handleFast()  { setSpeedFast();  server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
void handleHuman() { setSpeedHuman(); server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }

// ===================== 初始化 =====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n====== 系统启动 ======");
  Serial.println("[INFO] Serial初始化完成");

  randomSeed(analogRead(0));
  Serial.println("[INFO] 随机种子初始化");

  pinMode(SER, OUTPUT);
  pinMode(SRCLK, OUTPUT);
  pinMode(RCLK, OUTPUT);
  Serial.println("[INFO] 595引脚配置完成");
  allKeysOff();
  Serial.println("[INFO] 全部按键置低");
  setSpeedMid();
  Serial.println("[INFO] 默认中速模式");

  Serial.println("[INFO] WiFi进入AP+STA模式");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PWD);
  Serial.print("[SUCCESS] AP热点启动成功 SSID:");
  Serial.print(AP_SSID);
  Serial.print(" IP:");
  Serial.println(WiFi.softAPIP().toString());

  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  connectToSavedWiFi();
  wifiMonitor();

  server.on("/", sendHomePage);
  server.on("/scan", sendScanPage);
  server.on("/go", sendGoPage);
  server.on("/conn", handleConn);
  server.on("/run", handleRun);
  server.on("/copybuf", handleCopyBuf);
  server.on("/pastebuf", handlePasteBuf);
  server.on("/slow", handleSlow);
  server.on("/mid",  handleMid);
  server.on("/fast", handleFast);
  server.on("/human",handleHuman);
  server.begin();
  Serial.println("[SUCCESS] Web服务已启动");
  Serial.println("====== 系统就绪 ======");
}

void loop() {
  wifiMonitor();
  server.handleClient();
}
