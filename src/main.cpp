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
// ====================================================================================
// WiFi 热点配置
WebServer server(80);
const char* AP_SSID = "ESP32-KEYBOARD";
const char* AP_PWD  = "12345678";
// 全局状态
String connectedWiFiName = "未连接";
IPAddress localIP;
Preferences prefs;
// ======================== 速度配置（参数统一管理） ========================
int keyPressMs = 40;    // 按键按下时长
int keyDelayMs = 60;   // 按键间隔时长
bool humanMode = false; // 拟人模式
int currentMode = 1;   // 当前模式 0=慢速 1=中速 2=快速 3=拟人
// 获取速度名称
String getSpeedName() {
  switch(currentMode){
    case 0: return "慢速";
    case 1: return "中速";
    case 2: return "快速";
    case 3: return "拟人速";
    default: return "中速";
  }
}
// 速度模式设置
void setSpeedSlow()  { keyPressMs = 60; keyDelayMs = 120; humanMode = false; currentMode = 0; Serial.println("[SPEED] 设置为慢速"); }
void setSpeedMid()   { keyPressMs = 40; keyDelayMs = 60;  humanMode = false; currentMode = 1; Serial.println("[SPEED] 设置为中速"); }
void setSpeedFast()  { keyPressMs = 25; keyDelayMs = 30;  humanMode = false; currentMode = 2; Serial.println("[SPEED] 设置为快速"); }
void setSpeedHuman() { humanMode = true; currentMode = 3; Serial.println("[SPEED] 设置为拟人速"); }
// ====================================================================
// 连接保存的WiFi
bool connectToSavedWiFi() {
  Serial.println("[WiFi] 读取NVS保存WiFi");
  prefs.begin("wifi", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPWD = prefs.getString("pwd", "");
  prefs.end();
  if (savedSSID.isEmpty()) {
    Serial.println("[WiFi] NVS无保存WiFi");
    return false;
  }
  Serial.printf("[WiFi] 尝试连接SSID:%s\n", savedSSID.c_str());
  WiFi.begin(savedSSID.c_str(), savedPWD.c_str());
  for (int i = 0; i < 20; i++) {
    delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] STA连接成功 IP:");
      Serial.println(WiFi.localIP().toString());
      return true;
    }
  }
  Serial.println("[WiFi] STA连接超时");
  return false;
}
// WiFi状态监测（自动保存已连接WiFi）
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
// 74HC595 输出64位数据
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
// 所有按键松开
void allKeysOff() {
  send64bit(0);
}
// 单个按键按下
void keyPress(int ch) {
  if (ch < 0 || ch > 63) return;

  allKeysOff();
  delay(10);
  uint64_t mask = (uint64_t)1 << ch;
  send64bit(mask);

  // 按下延时
  humanMode ? delay(random(50,100)) : delay(keyPressMs);
  allKeysOff();
  // 间隔延时
  humanMode ? delay(random(80,250)) : delay(keyDelayMs);
}
// ====================== 核心优化：Shift组合键（适配中文输入法） ======================
// 时序：先按Shift → 再按字符 → 先松Shift → 再松字符
void shiftKeyPress(int ch) {
  if (ch < 0 || ch > 63) return;

  allKeysOff();
  delay(10);
  // 1. 单独按下 Shift
  uint64_t shiftMask = (uint64_t)1 << KEY_SHIFT;
  send64bit(shiftMask);
  delay(15);
  // 2. 按下目标字符
  uint64_t keyMask = shiftMask | ((uint64_t)1 << ch);
  send64bit(keyMask);
  // 按下保持时长
  humanMode ? delay(random(50,100)) : delay(keyPressMs);
  // 3. 先松开 Shift
  send64bit((uint64_t)1 << ch);
  delay(15);
  // 4. 松开字符
  allKeysOff();
  delay(10); // 【优化】硬件防抖，防止按键粘连
  // 间隔延时
  humanMode ? delay(random(100,250)) : delay(keyDelayMs);
}
// 判断是否需要Shift + 字符映射
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
// 按键通道映射表
int getKeyChannel(char c) {
  switch (c) {
    case '`':  return 0;
    case '1':  return 1; case '2': return 2; case '3': return 3; case '4': return 4;
    case '5':  return 5; case '6': return 6; case '7': return 7; case '8': return 8;
    case '9':  return 9; case '0': return 10; case '-': return 11; case '=': return 12;
    case 8:    return 13; // 退格
    case '\t': return 14;
    case 'q':  return 15; case 'w': return 16; case 'e': return 17; case 'r': return 18;
    case 't':  return 19; case 'y': return 20; case 'u': return 21; case 'i': return 22;
    case 'o':  return 23; case 'p': return 24; case '[': return 25; case ']': return 26;
    case '\\': return 27;
    case 'a':  return 29; case 's': return 30; case 'd': return 31; case 'f': return 32;
    case 'g':  return 33; case 'h': return 34; case 'j': return 35; case 'k': return 36;
    case 'l':  return 37; case ';': return 38; case '\'': return 39;
    case '\n': return KEY_ENTER; // 回车
    case 'z':  return 57; case 'x': return 56; case 'c': return 55; case 'v': return 54;
    case 'b':  return 53; case 'n': return 52; case 'm': return 51; case ',': return 50;
    case '.':  return 49; case '/': return 48; case ' ': return 47;

    default:   return -1;
  }
}
// ====================== 打字函数（适配IDEA缩进） ======================
void typeText(String s) {
  Serial.println("[TYPE] =====开始输出文本=====");
  Serial.printf("[TYPE] 总长度=%u, 原始内容=%s\n", s.length(), s.c_str());
  for (int i = 0; i < s.length(); i++) {
    char c = s[i];
    int ch;
    // Shift符号处理
    if (needShift(c, ch)) {
      shiftKeyPress(ch);
      continue;
    }
    // 大写字母处理
    if (c >= 'A' && c <= 'Z') {
      ch = getKeyChannel(c + 32);
      shiftKeyPress(ch);
      continue;
    }
    // 普通字符
    ch = getKeyChannel(c);
    if (ch >= 0) {
      keyPress(ch);
      // 修复IDEA缩进：延时等待IDE处理 + Home键回到行首
      if(c == '\n'){
        delay(80);
        keyPress(KEY_HOME);
      }
    }
  }
  Serial.println("[TYPE] =====输出文本完成=====");
}
// ===================== 网页界面（100%原样保留） =====================
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
/* 速度按钮 统一蓝色系 */
.speed-btn{
  padding:12px;
  text-align:left;
  font-size:14px;
  background:#4da6ff;  /* 未选中：淡蓝色（同色系） */
  border:none;
  color:white;
  width:100%;
  margin-bottom:6px;
}
/* 选中：和开始输入完全一样的颜色 */
.speed-btn.active{
  background:#007bff;
  font-weight:bold;
}
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
<!-- 速度折叠面板（和WiFi一样）标题显示当前速度 -->
<div class="accordion">
  <button class="accordion-header" onclick="toggleSpeed()">⚡ 打字速度 - 当前：)HTML"+speedName+R"HTML(</button>
  <div class="accordion-content" id="speedPanel">
    <button class="speed-btn )HTML"+(currentMode==0?"active":"")+R"HTML(" onclick="location.href='/slow'">🐢 慢速 (约 180 字符/分钟)</button>
    <button class="speed-btn )HTML"+(currentMode==1?"active":"")+R"HTML(" onclick="location.href='/mid'">⚡ 中速 (约 360 字符/分钟)</button>
    <button class="speed-btn )HTML"+(currentMode==2?"active":"")+R"HTML(" onclick="location.href='/fast'">🚀 快速 (约 600 字符/分钟)</button>
    <button class="speed-btn )HTML"+(currentMode==3?"active":"")+R"HTML(" onclick="location.href='/human'">👤 拟人速 (约 120~200 字符/分钟)</button>
  </div>
</div>
<!-- WiFi折叠面板 -->
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
<button type="submit" style="padding:10px 18px;background:#007bff">✅ 开始输入</button>
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
  // ==============================================
  // 核心修复：仅扫描当前STA信道 → STA绝对不断网
  // ==============================================
  int currentChannel = WiFi.channel(); // 获取当前连接的WiFi信道
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
<button type="submit" style="padding:10px 18px;background:#007bff">✅ 开始输入</button>
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
// Web请求处理函数
void handleConn() {
  Serial.println("[WEB] 收到 /conn 请求，WiFi连接");
  String ssid = server.arg("ssid");
  String pwd = server.arg("pwd");
  WiFi.disconnect(false);
  delay(200);
  WiFi.begin(ssid.c_str(), pwd.c_str());
  connectedWiFiName = "连接中...";

  for (int i = 0; i < 10; i++) {
    delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      connectedWiFiName = WiFi.SSID();
      localIP = WiFi.localIP();
      Serial.print("[WEB] WiFi连接成功 IP:");
      Serial.println(localIP.toString());
      break;
    }
  }
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}
void handleRun() {
  Serial.println("[WEB] 收到 /run 请求，执行typeText");
  typeText(server.arg("content"));
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}
void handleSlow()  { Serial.println("[WEB] /slow"); setSpeedSlow();  server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
void handleMid()   { Serial.println("[WEB] /mid"); setSpeedMid();   server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
void handleFast()  { Serial.println("[WEB] /fast"); setSpeedFast();  server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
void handleHuman() { Serial.println("[WEB] /human"); setSpeedHuman(); server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
// ===================== 初始化 =====================
void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\n========== SYSTEM BOOT START ==========");
  Serial.println("[SETUP] Serial init ok");

  randomSeed(analogRead(0));
  Serial.println("[SETUP] randomSeed done");

  // 595引脚初始化
  pinMode(SER, OUTPUT);
  pinMode(SRCLK, OUTPUT);
  pinMode(RCLK, OUTPUT);
  Serial.println("[SETUP] 74HC595 pinMode set");

  allKeysOff();
  Serial.println("[SETUP] allKeysOff() 全部按键关闭");

  setSpeedMid();
  Serial.println("[SETUP] 速度初始化完成");

  // WiFi初始化
  Serial.println("[SETUP] WiFi.mode(WIFI_AP_STA)");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PWD);
  Serial.printf("[SETUP] AP热点启动 SSID=%s IP=%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  connectToSavedWiFi();
  wifiMonitor();

  // Web路由注册
  server.on("/", sendHomePage);
  server.on("/scan", sendScanPage);
  server.on("/go", sendGoPage);
  server.on("/conn", handleConn);
  server.on("/run", handleRun);
  server.on("/slow", handleSlow);
  server.on("/mid",  handleMid);
  server.on("/fast", handleFast);
  server.on("/human",handleHuman);
  server.begin();
  Serial.println("[SETUP] WebServer 80端口启动完成");
  Serial.println("========== SYSTEM BOOT READY ==========\n");
}
void loop() {
  wifiMonitor();
  server.handleClient();
}