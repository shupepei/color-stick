// pio run -e pjt07_color_sensor -t upload
// [スマホからの接続手順]
// 1. スマホのWi-Fi設定で SSID: "M5_ColorSensor", パスワード: "12345678" に接続
// 2. ブラウザ(Safari/Chrome)を開き、http://192.168.4.1 にアクセスする
#include <Adafruit_TCS34725.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <Wire.h>
#include <cmath>
#include <WiFi.h>
#include <WebServer.h>

// I2C通信用の設定(AtomS3のGroveポートはSDA=2, SCL=1)
#define SDA_PIN 2
#define SCL_PIN 1

// TCS34725インスタンスの作成
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

// 不揮発性メモリ(NVS)用のインスタンス
Preferences prefs;

// 補正係数 (初期値)
float r_comp = 1.0f;
float g_comp = 1.0f;
float b_comp = 1.0f;

// --- Webサーバー関連 ---
WebServer server(80);

// WebUIへ渡すためのグローバル変数
uint16_t g_r = 0, g_g = 0, g_b = 0, g_c = 0;
uint8_t g_r8 = 0, g_g8 = 0, g_b8 = 0;
float g_h = 0.0f;
uint16_t g_temp = 0, g_lux = 0;

// スマホへ表示するHTMLページ (デザインも含む)
const char* html_page = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Color Monitor</title>
<style>
  body {
    background-color: #121212;
    color: #ffffff;
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 20px;
    margin: 0;
  }
  h2 { margin-bottom: 15px; font-weight: 400; letter-spacing: 1px; }
  .color-box {
    width: 90vw;
    height: 180px;
    border-radius: 16px;
    background-color: rgb(0,0,0);
    box-shadow: 0 8px 25px rgba(255,255,255,0.1);
    margin-bottom: 25px;
    transition: background-color 0.2s ease-out, box-shadow 0.2s ease-out;
  }
  .card {
    background: #1e1e1e;
    padding: 20px;
    border-radius: 16px;
    width: 90vw;
    box-sizing: border-box;
    box-shadow: 0 4px 15px rgba(0,0,0,0.5);
  }
  .data-row {
    display: flex;
    justify-content: space-between;
    margin: 12px 0;
    font-size: 1.15em;
    border-bottom: 1px solid #333;
    padding-bottom: 6px;
  }
  .data-row:last-child { border-bottom: none; }
  .label { color: #aaaaaa; }
  .value { font-weight: bold; }
  .hint { margin-top: 20px; color: #888; font-size: 0.9em; text-align: center; }
</style>
</head>
<body>
  <h2>Color Monitor</h2>
  <div class="color-box" id="colorBox"></div>
  <div class="card">
    <div class="data-row"><span class="label">Raw (R, G, B, C)</span><span class="value" id="valRaw">0, 0, 0, 0</span></div>
    <div class="data-row"><span class="label">Norm (R, G, B)</span><span class="value" id="valNorm">0, 0, 0</span></div>
    <div class="data-row"><span class="label">Hue</span><span class="value"><span id="valHue">0</span> &deg;</span></div>
    <div class="data-row"><span class="label">Color Temp</span><span class="value"><span id="valTemp">0</span> K</span></div>
    <div class="data-row"><span class="label">Lux</span><span class="value" id="valLux">0</span></div>
    <div class="data-row"><span class="label">Comp (R, G, B)</span><span class="value" id="valComp">1.0, 1.0, 1.0</span></div>
  </div>
  <div class="hint">AtomS3の画面を押すと白のキャリブレーションが行われます</div>

<script>
  function updateData() {
    fetch('/data').then(response => response.json()).then(data => {
      document.getElementById('colorBox').style.backgroundColor = `rgb(${data.r8}, ${data.g8}, ${data.b8})`;
      // 光っているようなエフェクト
      document.getElementById('colorBox').style.boxShadow = `0 8px 30px rgba(${data.r8}, ${data.g8}, ${data.b8}, 0.6)`;
      document.getElementById('valRaw').innerText = `${data.r}, ${data.g}, ${data.b}, ${data.c}`;
      document.getElementById('valNorm').innerText = `${data.r8}, ${data.g8}, ${data.b8}`;
      document.getElementById('valHue').innerText = data.h.toFixed(1);
      document.getElementById('valTemp').innerText = data.temp;
      document.getElementById('valLux').innerText = data.lux;
      document.getElementById('valComp').innerText = `${data.cr.toFixed(1)}, ${data.cg.toFixed(1)}, ${data.cb.toFixed(1)}`;
    }).catch(console.error);
  }
  // 200msごとにデータを更新
  setInterval(updateData, 200);
</script>
</body>
</html>
)rawliteral";

// ルートアクセス時にHTMLを返す
void handleRoot() {
  server.send(200, "text/html", html_page);
}

// 非同期通信でセンサの数値を返す(JSON形式)
void handleData() {
  char json[200];
  snprintf(json, sizeof(json), 
    "{\"r\":%d,\"g\":%d,\"b\":%d,\"c\":%d,\"r8\":%d,\"g8\":%d,\"b8\":%d,\"h\":%.1f,\"temp\":%d,\"lux\":%d,\"cr\":%.1f,\"cg\":%.1f,\"cb\":%.1f}",
    g_r, g_g, g_b, g_c, g_r8, g_g8, g_b8, g_h, g_temp, g_lux, r_comp, g_comp, b_comp);
  server.send(200, "application/json", json);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0; 
  M5.begin(cfg);
  
  Serial.begin(115200);

  M5.Display.setTextSize(1.5);
  M5.Display.println("Color Sensor");

  // --- Wi-Fi アクセスポイントの起動 ---
  WiFi.softAP("M5_ColorSensor", "12345678");
  IPAddress IP = WiFi.softAPIP();
  M5.Display.print("AP IP: ");
  M5.Display.println(IP);

  // Webサーバーのルーティング設定と開始
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  Wire.begin(SDA_PIN, SCL_PIN);

  if (tcs.begin()) {
    M5.Display.println("Sensor Found!");
  } else {
    M5.Display.println("No TCS34725 found...");
    while (1) delay(1000);
  }

  // NVS読み込み
  prefs.begin("color_calib", false);
  r_comp = prefs.getFloat("r_comp", 1.5f);
  g_comp = prefs.getFloat("g_comp", 1.0f);
  b_comp = prefs.getFloat("b_comp", 1.0f);

  delay(2000); // IPアドレスを見せるため少し待つ
  M5.Display.clear();
}

void loop() {
  M5.update();
  server.handleClient(); // Webサーバーのリクエストを処理

  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  
  // キャリブレーション処理
  if (M5.BtnA.wasPressed()) {
    if (c > 0 && r > 0 && g > 0 && b > 0) {
      float max_w = fmax(r, fmax(g, b));
      r_comp = max_w / r;
      g_comp = max_w / g;
      b_comp = max_w / b;

      prefs.putFloat("r_comp", r_comp);
      prefs.putFloat("g_comp", g_comp);
      prefs.putFloat("b_comp", b_comp);

      M5.Display.fillRect(0, 0, 128, 128, GREEN);
      M5.Display.setCursor(10, 50);
      M5.Display.setTextColor(BLACK);
      M5.Display.println("Calibrated!");
      M5.Display.setTextColor(WHITE);
      delay(1000);
      M5.Display.clear();
      return;
    }
  }

  if (c == 0) {
    delay(10); // 100msから10msに短縮し、Webサーバーの応答性を上げる
    return;
  }

  float r_cal = (float)r * r_comp;
  float g_cal = (float)g * g_comp;
  float b_cal = (float)b * b_comp;

  float max_val = fmax(r_cal, fmax(g_cal, b_cal));
  if (max_val == 0) max_val = 1;

  uint8_t r8 = (uint8_t)(r_cal / max_val * 255.0f);
  uint8_t g8 = (uint8_t)(g_cal / max_val * 255.0f);
  uint8_t b8 = (uint8_t)(b_cal / max_val * 255.0f);

  float fr = r8, fg = g8, fb = b8;
  float fcmax = fmax(fr, fmax(fg, fb));
  float fcmin = fmin(fr, fmin(fg, fb));
  float diff = fcmax - fcmin;
  float h = 0.0f;

  if (diff > 0.0f) {
    if (fcmax == fr) {
      h = 60.0f * ((fg - fb) / diff);
      if (h < 0.0f) h += 360.0f;
    } else if (fcmax == fg) {
      h = 60.0f * ((fb - fr) / diff) + 120.0f;
    } else {
      h = 60.0f * ((fr - fg) / diff) + 240.0f;
    }
  }

  uint16_t colorTemp = tcs.calculateColorTemperature(r, g, b);
  uint16_t lux = tcs.calculateLux(r, g, b);

  // グローバル変数へ保存 (Webサーバー用)
  g_r = r; g_g = g; g_b = b; g_c = c;
  g_r8 = r8; g_g8 = g8; g_b8 = b8;
  g_h = h;
  g_temp = colorTemp; g_lux = lux;

  // 画面描画
  M5.Display.fillRect(0, 0, 128, 90, BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextSize(1.5);
  M5.Display.printf("R: %-3d\n", r8);
  M5.Display.printf("G: %-3d\n", g8);
  M5.Display.printf("B: %-3d\n", b8);
  M5.Display.printf("Hue: %-5.1f\n", h);
  
  M5.Display.setTextSize(1.0);
  M5.Display.printf("comp:%.1f, %.1f, %.1f\n", r_comp, g_comp, b_comp);
  M5.Display.printf("[Press Scr to Calib]\n");

  uint16_t color565 = M5.Display.color565(r8, g8, b8);
  M5.Display.fillRect(0, 90, 128, 38, color565);

  Serial.printf("Raw[R:%d, G:%d, B:%d, C:%d] | ", r, g, b, c);
  Serial.printf("Norm[R:%d, G:%d, B:%d] | ", r8, g8, b8);
  Serial.printf("Hue:%.1f | Temp:%dK | Lux:%d\n", h, colorTemp, lux);

  delay(50); // Webサーバーの応答性を上げるため少し短めに
}
