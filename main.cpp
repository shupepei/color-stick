// pio run -e pjt07_color_sensor -t upload
#include "web_ui.h"
#include <Adafruit_TCS34725.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <M5Unified.h>
#include <OneButton.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <cmath>

WebServer server(80);

// --- ハードウェアピン設定 ---
#define SDA_PIN 2
#define SCL_PIN 1
#define LED_PIN 5
#define NUM_LEDS 10 // LED数はここから変更可能
#define HUE_TOLERANCE_GAME                                                     \
  30.0f // ゲームの正解判定の緩さ（色相の許容誤差、0〜180）初期値: 30度
        // (以前は10度)
#define HUE_TOLERANCE_TRIAD                                                    \
  20.0f // 虹色モード発動の緩さ（120度からの許容誤差）初期値: 20度 (以前は15度)
#define BUZZER_PIN 6
#define BUTTON_PIN 41 // ATOM S3の画面ボタン

// --- グローバルオブジェクト ---
CRGB leds[NUM_LEDS];
Adafruit_TCS34725 tcs =
    Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);
OneButton btn(BUTTON_PIN, true, true);
Preferences prefs;

// --- 省電力設定 ---
#define AUTO_POWER_OFF_MS 180000 // 3分間操作がなければ電源オフ
unsigned long lastActivityTime = 0;

// --- キャリブレーション設定 ---
float r_comp = 2.5f;
float g_comp = 1.3f;
float b_comp = 1.0f;
bool isCalibrating = false;

// --- 状態管理 ---
enum Mode { MODE_GAME, MODE_LIGHT };
Mode currentMode = MODE_GAME;

struct PickedColor {
  float h, s, v;
  uint8_t r, g, b;
};

// Mode 1: Game 用の変数
int targetHue = 0;
PickedColor pickedColorsGame[2];
int pickCountGame = 0;
bool showGameResult = false;
bool lastGameResult = false;
float lastGameResultDiff = 0.0f;
float lastGameResultMixHue = 0.0f;

// Mode 2: Light 用の変数
CRGB pickedColorsLight[3];
int pickCountLight = 0;
bool isRainbowMode = false;

// ブザー（非同期制御用）変数
unsigned long buzzStartTime = 0;
bool isBuzzing = false;
int buzzStep = 0;

// ==========================================
// ユーティリティ関数
// ==========================================

// RGB -> HSV 変換 (H: 0-360, S: 0-100, V: 0-100)
void RGBtoHSV(float r, float g, float b, float &h, float &s, float &v) {
  float cmax = fmax(r, fmax(g, b));
  float cmin = fmin(r, fmin(g, b));
  float diff = cmax - cmin;

  v = cmax / 255.0f * 100.0f;
  if (cmax == 0) {
    s = 0;
  } else {
    s = (diff / cmax) * 100.0f;
  }

  if (diff == 0) {
    h = 0;
  } else if (cmax == r) {
    h = fmod((60 * ((g - b) / diff) + 360), 360.0f);
  } else if (cmax == g) {
    h = fmod((60 * ((b - r) / diff) + 120), 360.0f);
  } else if (cmax == b) {
    h = fmod((60 * ((r - g) / diff) + 240), 360.0f);
  }
}

// 0-360の円環構造を考慮したHue差分計算
float getHueDifference(float h1, float h2) {
  float diff = fabs(h1 - h2);
  if (diff > 180.0f) {
    diff = 360.0f - diff;
  }
  return diff;
}

// 簡易的なHue -> rgb565 変換用 (UI描画用)
uint16_t hsvToColor565(float h, float s, float v) {
  CHSV hsv((uint8_t)(h / 360.0f * 255.0f), (uint8_t)(s / 100.0f * 255.0f),
           (uint8_t)(v / 100.0f * 255.0f));
  CRGB rgb;
  hsv2rgb_rainbow(hsv, rgb);
  return M5.Display.color565(rgb.r, rgb.g, rgb.b);
}

// RGB <-> RYB 変換 (絵の具の混色シミュレーション用)
float rgbToRyb(float h) {
  h = fmod(h, 360.0f);
  if (h < 0) h += 360.0f;
  if (h < 60.0f) return h * 2.0f;
  if (h < 120.0f) return 120.0f + (h - 60.0f) * 1.0f;
  if (h < 240.0f) return 180.0f + (h - 120.0f) * 0.5f;
  return 240.0f + (h - 240.0f) * 1.0f;
}

float rybToRgb(float h) {
  h = fmod(h, 360.0f);
  if (h < 0) h += 360.0f;
  if (h < 120.0f) return h * 0.5f;
  if (h < 180.0f) return 60.0f + (h - 120.0f) * 1.0f;
  if (h < 240.0f) return 120.0f + (h - 180.0f) * 2.0f;
  return 240.0f + (h - 240.0f) * 1.0f;
}

// HSV -> RGB 完全変換用
void HSVtoRGB(float h, float s, float v, uint8_t &r, uint8_t &g, uint8_t &b) {
  float c = (v / 100.0f) * (s / 100.0f);
  float h_prime = h / 60.0f;
  float x = c * (1.0f - fabs(fmod(h_prime, 2.0f) - 1.0f));
  float m = (v / 100.0f) - c;

  float r_f, g_f, b_f;
  if (h_prime >= 0 && h_prime < 1) { r_f = c; g_f = x; b_f = 0; }
  else if (h_prime >= 1 && h_prime < 2) { r_f = x; g_f = c; b_f = 0; }
  else if (h_prime >= 2 && h_prime < 3) { r_f = 0; g_f = c; b_f = x; }
  else if (h_prime >= 3 && h_prime < 4) { r_f = 0; g_f = x; b_f = c; }
  else if (h_prime >= 4 && h_prime < 5) { r_f = x; g_f = 0; b_f = c; }
  else { r_f = c; g_f = 0; b_f = x; }

  r = (uint8_t)((r_f + m) * 255.0f);
  g = (uint8_t)((g_f + m) * 255.0f);
  b = (uint8_t)((b_f + m) * 255.0f);
}

// ==========================================
// センサー処理
// ==========================================
bool readSensor(float &h, float &s, float &v, CRGB &rgbColor) {
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);

  // 暗闇またはエラー時
  if (c == 0)
    return false;

  // ホワイトバランス補正
  float r_cal = (float)r * r_comp;
  float g_cal = (float)g * g_comp;
  float b_cal = (float)b * b_comp;

  // 正規化 (0-255)
  float max_val = fmax(r_cal, fmax(g_cal, b_cal));
  if (max_val == 0)
    max_val = 1;

  uint8_t r8 = (uint8_t)(r_cal / max_val * 255.0f);
  uint8_t g8 = (uint8_t)(g_cal / max_val * 255.0f);
  uint8_t b8 = (uint8_t)(b_cal / max_val * 255.0f);

  RGBtoHSV(r8, g8, b8, h, s, v);

  // --- 白っぽさを軽減する補正（彩度ブースト） ---
  // 彩度(S)を強調することで、よりシャープでビビッドな色にする
  s = fmin(100.0f, s * 1.5f); // 1.5倍に強調（最大100%）

  // 補正した彩度でRGBを再計算
  HSVtoRGB(h, s, v, r8, g8, b8);
  rgbColor = CRGB(r8, g8, b8);

  return true;
}

// ==========================================
// UI・リセット処理
// ==========================================
void drawUI() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);

  if (isCalibrating) {
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1.0);
    M5.Display.println("しろいろを\nおぼえさせるよ");
    M5.Display.setTextColor(TFT_YELLOW);
    M5.Display.setCursor(0, 50);
    M5.Display.println("しろいものに\nむけて");
    M5.Display.println("ボタンをおして！");
    return;
  }

  if (currentMode == MODE_GAME) {
    if (showGameResult) {
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextSize(1.5);
      if (lastGameResult) {
        M5.Display.setTextColor(TFT_GREEN);
        M5.Display.println("せいかい！");
      } else {
        M5.Display.setTextColor(TFT_RED);
        M5.Display.println("ざんねん…");
      }
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setTextSize(1.0);

      // 目標の色
      uint16_t tColor = hsvToColor565((float)targetHue, 100.0f, 100.0f);
      M5.Display.setCursor(0, 25);
      M5.Display.print("もくひょう:");
      M5.Display.fillRect(95, 23, 25, 12, tColor);

      // 作った(採取した)色
      uint16_t pColor = hsvToColor565(lastGameResultMixHue, 100.0f, 100.0f);
      M5.Display.setCursor(0, 40);
      M5.Display.print("つくった色:");
      M5.Display.fillRect(95, 38, 25, 12, pColor);

      M5.Display.setCursor(0, 55);
      M5.Display.printf("ズレ: %.1f度\n", lastGameResultDiff);

      M5.Display.setCursor(0, 70);
      M5.Display.println("クリックで次へ");

      // --- Hue Bar の描画 (y=90〜100) ---
      for (int i = 0; i < 120; i++) {
        float ryb_hue = (float)i / 120.0f * 360.0f;
        float rgb_hue = rybToRgb(ryb_hue); // RYBベースのグラデーションにするため変換
        uint16_t color = hsvToColor565(rgb_hue, 100.0f, 100.0f);
        M5.Display.drawFastVLine(4 + i, 90, 10, color);
      }

      // 目標の色のマーカー (▽) (RYB空間の位置に配置)
      int targetX = 4 + (int)(rgbToRyb((float)targetHue) / 360.0f * 120.0f);
      M5.Display.fillTriangle(targetX - 4, 85, targetX + 4, 85, targetX, 89,
                              TFT_WHITE);

      // 採取(混色)した色のマーカー (△) (RYB空間の位置に配置)
      int pickedX = 4 + (int)(rgbToRyb(lastGameResultMixHue) / 360.0f * 120.0f);
      M5.Display.fillTriangle(pickedX - 4, 105, pickedX + 4, 105, pickedX, 101,
                              TFT_WHITE);

      return;
    }

    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setTextSize(1.0);
    M5.Display.println("いろさがし");

    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.printf("もくひょう: %d\n", targetHue);

    // Target Color Rect
    uint16_t targetColor = hsvToColor565(targetHue, 100.0f, 100.0f);
    M5.Display.fillRect(10, 30, 30, 30, targetColor);

    // --- Hue Bar の描画 (y=67〜74) ---
    for (int i = 0; i < 120; i++) {
      float ryb_hue = (float)i / 120.0f * 360.0f;
      float rgb_hue = rybToRgb(ryb_hue);
      uint16_t color = hsvToColor565(rgb_hue, 100.0f, 100.0f);
      M5.Display.drawFastVLine(4 + i, 67, 8, color);
    }

    // 目標の色のマーカー (▽)
    int targetX = 4 + (int)(rgbToRyb((float)targetHue) / 360.0f * 120.0f);
    M5.Display.fillTriangle(targetX - 4, 62, targetX + 4, 62, targetX, 66, TFT_WHITE);

    // 採取した色のマーカー (△)
    for (int i = 0; i < pickCountGame; i++) {
      int pickedX = 4 + (int)(rgbToRyb(pickedColorsGame[i].h) / 360.0f * 120.0f);
      M5.Display.fillTriangle(pickedX - 4, 79, pickedX + 4, 79, pickedX, 75, TFT_WHITE);
    }

    M5.Display.setCursor(0, 82);
    M5.Display.printf("あつめた数: %d/2\n", pickCountGame);

    // 採取した色を表示
    for (int i = 0; i < pickCountGame; i++) {
      uint16_t c = M5.Display.color565(
          pickedColorsGame[i].r, pickedColorsGame[i].g, pickedColorsGame[i].b);
      M5.Display.fillRect(10 + i * 35, 98, 25, 12, c);
    }

    if (pickCountGame == 2) {
      M5.Display.setCursor(0, 112);
      M5.Display.setTextColor(TFT_YELLOW);
      M5.Display.setTextSize(1.0);
      M5.Display.println("クリックで判定！");
    }

  } else {
    // MODE_LIGHT
    M5.Display.setTextColor(TFT_ORANGE);
    M5.Display.setTextSize(1.0);
    M5.Display.println("ライト");
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.printf("あつめた数: %d/3\n", pickCountLight);

    // 採取した色を表示
    for (int i = 0; i < pickCountLight; i++) {
      uint16_t c =
          M5.Display.color565(pickedColorsLight[i].r, pickedColorsLight[i].g,
                              pickedColorsLight[i].b);
      M5.Display.fillRect(10 + i * 35, 40, 30, 30, c);
    }

    if (isRainbowMode) {
      M5.Display.setCursor(0, 90);
      M5.Display.setTextColor(TFT_MAGENTA);
      M5.Display.println("にじいろモード！");
    }
  }
}

void resetGame() {
  // RYB空間でランダム生成することで、オレンジや紫といった子供に馴染みのある色が出やすくなる
  targetHue = (int)rybToRgb((float)random(0, 360));
  pickCountGame = 0;
  showGameResult = false;
  drawUI();
}

void resetLight() {
  pickCountLight = 0;
  isRainbowMode = false;
  FastLED.clear();
  FastLED.show();
  drawUI();
}

// ==========================================
// ブザー非同期処理
// ==========================================
enum SoundType { SOUND_NONE, SOUND_SUCCESS, SOUND_FAILURE, SOUND_RAINBOW };
SoundType currentSound = SOUND_NONE;

void triggerSound(SoundType type) {
  currentSound = type;
  isBuzzing = true;
  buzzStep = 0;
}

void playSoundTask() {
  if (!isBuzzing)
    return;

  unsigned long now = millis();
  if (currentSound == SOUND_SUCCESS || currentSound == SOUND_RAINBOW) {
    if (buzzStep == 0) {
      ledcWriteTone(1, 2093); // C7 (ド) - ピエゾが鳴りやすい高い音に
      buzzStartTime = now;
      buzzStep = 1;
    } else if (buzzStep == 1 && now - buzzStartTime >= 100) {
      ledcWriteTone(1, 2637); // E7 (ミ)
      buzzStartTime = now;
      buzzStep = 2;
    } else if (buzzStep == 2 && now - buzzStartTime >= 300) {
      ledcWriteTone(1, 0); // 停止
      isBuzzing = false;
      buzzStep = 0;
    }
  } else if (currentSound == SOUND_FAILURE) {
    if (buzzStep == 0) {
      ledcWriteTone(1, 150); // 1音目 低音150Hz
      buzzStartTime = now;
      buzzStep = 1;
    } else if (buzzStep == 1 && now - buzzStartTime >= 150) {
      ledcWriteTone(1, 0); // 無音
      buzzStartTime = now;
      buzzStep = 2;
    } else if (buzzStep == 2 &&
               now - buzzStartTime >=
                   150) {    // 間を長く(50ms➔150ms)して振動を完全に止める
      ledcWriteTone(1, 150); // 2音目150Hz
      buzzStartTime = now;
      buzzStep = 3;
    } else if (buzzStep == 3 &&
               now - buzzStartTime >=
                   200) {  // 2音目も少し短く(250ms➔200ms)して負担軽減
      ledcWriteTone(1, 0); // 停止
      isBuzzing = false;
      buzzStep = 0;
    }
  }
}

// ==========================================
// ボタンコールバック
// ==========================================
void onSingleClick() {
  lastActivityTime = millis();
  // キャリブレーション実行時
  if (isCalibrating) {
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);
    if (c > 0 && r > 0 && g > 0 && b > 0) {
      float max_w = fmax(r, fmax(g, b));
      r_comp = max_w / r;
      g_comp = max_w / g;
      b_comp = max_w / b;

      // NVSに保存
      prefs.putFloat("r_comp", r_comp);
      prefs.putFloat("g_comp", g_comp);
      prefs.putFloat("b_comp", b_comp);

      isCalibrating = false;
      M5.Display.clear(TFT_GREEN);
      M5.Display.setCursor(10, 50);
      M5.Display.setTextColor(TFT_BLACK);
      M5.Display.setTextSize(1.5);
      M5.Display.println("おぼえたよ！");
      ledcWriteTone(1, 2000);
      delay(100);
      ledcWriteTone(1, 0);
      delay(900); // UIとして見せるため (ここだけ例外的にdelay)
      resetGame();
    }
    return;
  }

  // 通常時の色採取
  float h, s, v;
  CRGB rgb;
  if (readSensor(h, s, v, rgb)) {
    if (currentMode == MODE_GAME) {
      if (showGameResult) {
        resetGame();
        return;
      }

      if (pickCountGame < 2) {
        pickedColorsGame[pickCountGame] = {h, s, v, rgb.r, rgb.g, rgb.b};
        pickCountGame++;

        drawUI();
      } else if (pickCountGame == 2) {
        // 減法混色（RYB）のシミュレーション
        float h1 = pickedColorsGame[0].h;
        float s1 = pickedColorsGame[0].s;
        float v1 = pickedColorsGame[0].v;
        float h2 = pickedColorsGame[1].h;
        float s2 = pickedColorsGame[1].s;
        float v2 = pickedColorsGame[1].v;

        // RGBの色相をRYB（絵の具）の色相に変換
        float ryb1 = rgbToRyb(h1);
        float ryb2 = rgbToRyb(h2);

        // 円座標(極座標)でのベクトル合成（彩度をベクトルの長さとして重み付け）
        float rad1 = ryb1 * PI / 180.0f;
        float rad2 = ryb2 * PI / 180.0f;
        float x = (s1 * cos(rad1)) + (s2 * cos(rad2));
        float y = (s1 * sin(rad1)) + (s2 * sin(rad2));

        float mix_ryb_h = atan2(y, x) * 180.0f / PI;
        if (mix_ryb_h < 0) mix_ryb_h += 360.0f;

        // RYBの色相をRGBに戻す
        float mix_h = rybToRgb(mix_ryb_h);

        // 彩度と明度は平均をとる
        float mix_s = (s1 + s2) / 2.0f;
        float mix_v = (v1 + v2) / 2.0f;

        // 判定（ズレ）はRYB空間の角度で計算する
        float diff = getHueDifference(mix_ryb_h, rgbToRyb((float)targetHue));
        lastGameResultDiff = diff;
        lastGameResultMixHue = mix_h;

        if (diff <= HUE_TOLERANCE_GAME) {
          lastGameResult = true;
          triggerSound(SOUND_SUCCESS);
        } else {
          lastGameResult = false;
          triggerSound(SOUND_FAILURE);
        }

        showGameResult = true;
        drawUI();
      }
    } else {
      // MODE_LIGHT
      if (pickCountLight < 3) {
        pickedColorsLight[pickCountLight] = rgb;
        pickCountLight++;

        // 隠しモード（トライアド配色判定）
        if (pickCountLight == 3) {
          float h_arr[3];
          for (int i = 0; i < 3; i++) {
            float temp_h, temp_s, temp_v;
            RGBtoHSV((float)pickedColorsLight[i].r,
                     (float)pickedColorsLight[i].g,
                     (float)pickedColorsLight[i].b, temp_h, temp_s, temp_v);
            // RYB空間での色相に変換して判定する
            h_arr[i] = rgbToRyb(temp_h);
          }

          float d1 = getHueDifference(h_arr[0], h_arr[1]);
          float d2 = getHueDifference(h_arr[1], h_arr[2]);
          float d3 = getHueDifference(h_arr[2], h_arr[0]);

          // それぞれが120±HUE_TOLERANCE_TRIAD度の間にあるか
          float min_d = 120.0f - HUE_TOLERANCE_TRIAD;
          float max_d = 120.0f + HUE_TOLERANCE_TRIAD;
          if (d1 >= min_d && d1 <= max_d && d2 >= min_d && d2 <= max_d &&
              d3 >= min_d && d3 <= max_d) {
            isRainbowMode = true;
            triggerSound(SOUND_RAINBOW); // 隠しモード成功音
          }
        }
        drawUI();
      }
    }
  }
}

// モード切り替え
void onDoubleClick() {
  lastActivityTime = millis();
  if (isCalibrating)
    return; // キャリブレーション中は無効

  if (currentMode == MODE_GAME) {
    currentMode = MODE_LIGHT;
    resetLight();
  } else {
    currentMode = MODE_GAME;
    resetGame();
  }
}

// リセット (手動リセット用)
void onLongPress() {
  lastActivityTime = millis();
  if (isCalibrating) {
    isCalibrating = false;
    resetGame();
    return;
  }

  if (currentMode == MODE_GAME) {
    resetGame();
  } else {
    resetLight();
  }
}

// ==========================================
// LEDアニメーション（非同期処理）
// ==========================================
void processLEDAnimation() {
  if (currentMode == MODE_GAME) {
    // ゲームモード中は基本消灯（正解時に光らせるなどの拡張も可能）
    FastLED.clear();
    FastLED.show();
    return;
  }

  // MODE_LIGHT
  if (isRainbowMode) {
    // レインボーアニメーションを最優先で処理
    uint8_t hue = millis() / 10;
    fill_rainbow(leds, NUM_LEDS, hue, 255 / NUM_LEDS);
    FastLED.show();

  } else if (pickCountLight == 0) {
    FastLED.clear();
    FastLED.show();

  } else if (pickCountLight == 1) {
    // 単色点灯
    fill_solid(leds, NUM_LEDS, pickedColorsLight[0]);
    FastLED.show();

  } else {
    // クロスフェード (2色または3色)
    unsigned long period = 2000; // 2秒で1サイクル
    unsigned long t = millis() % period;
    float progress = (float)t / period; // 0.0 ~ 1.0

    CRGB color;
    if (pickCountLight == 2) {
      if (progress < 0.5f) {
        // 0 -> 1
        color = blend(pickedColorsLight[0], pickedColorsLight[1],
                      (progress * 2.0f) * 255);
      } else {
        // 1 -> 0
        color = blend(pickedColorsLight[1], pickedColorsLight[0],
                      ((progress - 0.5f) * 2.0f) * 255);
      }
    } else {
      // 3色クロスフェード (レインボー失敗時)
      if (progress < 0.333f) {
        color = blend(pickedColorsLight[0], pickedColorsLight[1],
                      (progress * 3.0f) * 255);
      } else if (progress < 0.666f) {
        color = blend(pickedColorsLight[1], pickedColorsLight[2],
                      ((progress - 0.333f) * 3.0f) * 255);
      } else {
        color = blend(pickedColorsLight[2], pickedColorsLight[0],
                      ((progress - 0.666f) * 3.0f) * 255);
      }
    }
    fill_solid(leds, NUM_LEDS, color);
    FastLED.show();
  }
}

// ==========================================
// WebServer (WiFi AP) ハンドラ
// ==========================================
void handleRoot() { server.send(200, "text/html", INDEX_HTML); }

void handleApiState() {
  StaticJsonDocument<1024> doc;

  doc["mode"] = currentMode;
  doc["isCalibrating"] = isCalibrating;
  doc["targetHue"] = targetHue;
  doc["pickCountGame"] = pickCountGame;

  JsonArray gameColors = doc.createNestedArray("pickedColorsGame");
  for (int i = 0; i < pickCountGame; i++) {
    JsonObject c = gameColors.createNestedObject();
    c["h"] = pickedColorsGame[i].h;
    c["s"] = pickedColorsGame[i].s;
    c["v"] = pickedColorsGame[i].v;
    c["r"] = pickedColorsGame[i].r;
    c["g"] = pickedColorsGame[i].g;
    c["b"] = pickedColorsGame[i].b;
  }

  doc["showGameResult"] = showGameResult;
  doc["lastGameResult"] = lastGameResult;
  doc["lastGameResultDiff"] = lastGameResultDiff;
  doc["lastGameResultMixHue"] = lastGameResultMixHue;

  doc["pickCountLight"] = pickCountLight;

  JsonArray lightColors = doc.createNestedArray("pickedColorsLight");
  for (int i = 0; i < pickCountLight; i++) {
    JsonObject c = lightColors.createNestedObject();
    c["r"] = pickedColorsLight[i].r;
    c["g"] = pickedColorsLight[i].g;
    c["b"] = pickedColorsLight[i].b;
  }

  doc["isRainbowMode"] = isRainbowMode;

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleApiRainbow() {
  if (currentMode == MODE_LIGHT) {
    isRainbowMode = true;
    triggerSound(SOUND_RAINBOW);
    drawUI(); // スマホからの操作をM5の画面に即時反映
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Not in Light Mode");
  }
}

void setupWiFi() {
  WiFi.softAP("ColorCollector", ""); // パスワードなしオープン
  server.on("/", handleRoot);
  server.on("/api/state", handleApiState);
  server.on("/api/rainbow", handleApiRainbow);
  server.begin();
}

// ==========================================
// Setup & Loop
// ==========================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  // 日本語フォントの設定
  M5.Display.setFont(&fonts::lgfxJapanGothic_16);

  // センサー初期化
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!tcs.begin()) {
    M5.Display.println("No TCS34725 found...");
    while (1)
      delay(100);
  }

  // LED初期化
  FastLED.addLeds<WS2813, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(100);

  // ブザー (PWM) 初期化: ch1, 2kHz, 8bit
  ledcSetup(1, 2000, 8);
  ledcAttachPin(BUZZER_PIN, 1);
  ledcWriteTone(1, 0); // ミュート

  // ボタン設定
  btn.attachClick(onSingleClick);
  btn.attachDoubleClick(onDoubleClick);
  btn.attachLongPressStart(onLongPress);

  // NVSからのホワイトバランス読み込み
  prefs.begin("color_calib", false);
  r_comp = prefs.getFloat("r_comp", 1.0f);
  g_comp = prefs.getFloat("g_comp", 1.0f);
  b_comp = prefs.getFloat("b_comp", 1.0f);

  // WiFiAP・Webサーバー起動
  setupWiFi();

  // 起動時のキャリブレーション判定 (ボタン押しっぱなしで起動した場合のみ)
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(50);
  if (digitalRead(BUTTON_PIN) == LOW) {
    isCalibrating = true;
    drawUI();
  } else {
    isCalibrating = false;
    resetGame();
  }
}

void loop() {
  M5.update();
  btn.tick(); // ボタン状態の更新処理

  // オートパワーオフ判定
  if (millis() - lastActivityTime > AUTO_POWER_OFF_MS) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(1.5);
    M5.Display.setCursor(10, 50);
    M5.Display.println("Sleep...");
    delay(1000);
    M5.Power.powerOff();
  }

  playSoundTask();       // 非同期ブザー処理
  processLEDAnimation(); // 非同期LEDアニメーション処理

  server.handleClient(); // Webサーバーリクエスト処理

  delay(10); // ウォッチドッグタイマへの配慮
}
