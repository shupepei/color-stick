#ifndef WEB_UI_H
#define WEB_UI_H

const char* INDEX_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="ja">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Color Collector Dashboard</title>
    <style>
        @import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;600;800&display=swap');
        body {
            margin: 0; padding: 0; font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #0f2027, #203a43, #2c5364);
            color: white; min-height: 100vh; display: flex; flex-direction: column; align-items: center;
        }
        .container {
            width: 90%; max-width: 500px; margin-top: 30px;
            background: rgba(255, 255, 255, 0.05);
            backdrop-filter: blur(15px); -webkit-backdrop-filter: blur(15px);
            border: 1px solid rgba(255,255,255,0.1); border-radius: 20px;
            padding: 30px; box-shadow: 0 8px 32px 0 rgba(0,0,0,0.3);
            transition: all 0.3s ease;
        }
        h1 { text-align: center; font-weight: 800; font-size: 1.8rem; margin-bottom: 20px; letter-spacing: 1px; }
        .mode-badge {
            background: rgba(0, 255, 200, 0.2); color: #00ffc8; padding: 5px 15px; border-radius: 50px;
            font-size: 0.9rem; font-weight: 600; display: inline-block; margin-bottom: 20px;
            text-transform: uppercase; border: 1px solid rgba(0,255,200,0.3);
        }
        .color-box {
            width: 100%; height: 60px; border-radius: 12px; margin: 10px 0;
            box-shadow: inset 0 0 10px rgba(0,0,0,0.5);
            display: flex; align-items: center; justify-content: center; font-weight: bold; text-shadow: 1px 1px 3px rgba(0,0,0,0.8);
            transition: background 0.3s ease;
        }
        .section { margin-bottom: 25px; }
        .section-title { font-size: 1rem; color: #ccc; margin-bottom: 10px; font-weight: 600; }
        .status-text { font-size: 1.2rem; font-weight: bold; text-align: center; padding: 15px; border-radius: 12px; }
        .success { background: rgba(0, 255, 100, 0.2); color: #00ff64; border: 1px solid rgba(0,255,100,0.3); }
        .failure { background: rgba(255, 50, 50, 0.2); color: #ff3232; border: 1px solid rgba(255,50,50,0.3); }
        .hue-bar {
            width: 100%; height: 20px; background: linear-gradient(to right, #f00 0%, #f80 17%, #ff0 33%, #0f0 50%, #00f 67%, #808 83%, #f00 100%);
            border-radius: 10px; position: relative; margin-top: 30px; margin-bottom: 20px; box-shadow: 0 4px 10px rgba(0,0,0,0.3);
        }
        .marker {
            position: absolute; width: 4px; height: 30px; background: white; top: -5px; border-radius: 2px;
            box-shadow: 0 0 5px rgba(0,0,0,0.8); transition: left 0.3s ease;
        }
        .marker-target { background: #fff; z-index: 2;}
        .marker-picked { background: #000; border: 1px solid #fff; z-index: 3;}
        .flex-row { display: flex; gap: 10px; }
        .flex-1 { flex: 1; }
        .btn-rainbow {
            background: linear-gradient(to right, #f00, #ff0, #0f0, #0ff, #00f, #f0f);
            color: white; border: none; padding: 12px 24px; border-radius: 50px;
            font-size: 1rem; font-weight: bold; cursor: pointer; text-shadow: 1px 1px 2px rgba(0,0,0,0.8);
            box-shadow: 0 4px 15px rgba(0,0,0,0.4); transition: transform 0.2s; margin-top: 20px;
            width: 100%; display: none;
        }
        .btn-rainbow:active { transform: scale(0.95); }
        #loader { font-size: 0.8rem; color: #aaa; text-align: center; margin-top: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>Color Collector</h1>
        <div style="text-align: center;">
            <div class="mode-badge" id="mode-badge">Connecting...</div>
        </div>

        <div id="game-view" style="display: none;">
            <div class="section">
                <div class="section-title">もくひょうの色</div>
                <div class="color-box" id="target-color">Hue: --</div>
            </div>
            
            <div class="section">
                <div class="section-title">あつめた色 (<span id="pick-count">0</span>/2)</div>
                <div class="flex-row">
                    <div class="color-box flex-1" id="picked-1">Empty</div>
                    <div class="color-box flex-1" id="picked-2">Empty</div>
                </div>
            </div>

            <div class="section" id="result-section" style="display: none;">
                <div class="section-title">判定結果</div>
                <div id="result-status" class="status-text">--</div>
                
                <div class="hue-bar">
                    <div class="marker marker-target" id="marker-target" title="もくひょう"></div>
                    <div class="marker marker-picked" id="marker-picked" title="つくった色"></div>
                </div>
                <div style="text-align: center; color: #ccc; font-size: 0.9rem;">
                    ズレ: <span id="diff-val" style="color:white; font-weight:bold;">--</span> 度
                </div>
            </div>
        </div>

        <div id="light-view" style="display: none;">
            <div class="section">
                <div class="section-title">あつめた色 (ライトモード)</div>
                <div class="flex-row">
                    <div class="color-box flex-1" id="light-1">1</div>
                    <div class="color-box flex-1" id="light-2">2</div>
                    <div class="color-box flex-1" id="light-3">3</div>
                </div>
                <button id="btn-trigger-rainbow" class="btn-rainbow" onclick="triggerRainbow()">🌈 にじいろモードにする</button>
            </div>
            <div id="rainbow-status" class="status-text success" style="display: none;">
                🌈 にじいろモード 発動中！
            </div>
        </div>

        <div id="loader">Syncing with device...</div>
    </div>

    <script>
        function hsvToRgb(h, s, v) {
            let f = (n,k=(n+h/60)%6) => v - v*s*Math.max(Math.min(k,4-k,1),0);
            return [f(5)*255, f(3)*255, f(1)*255];
        }
        function rgbToRyb(h) {
            h = ((h % 360) + 360) % 360;
            if (h < 60) return h * 2;
            if (h < 120) return 120 + (h - 60);
            if (h < 240) return 180 + (h - 120) * 0.5;
            return 240 + (h - 240);
        }
        function triggerRainbow() {
            fetch('/api/rainbow', { method: 'POST' }).catch(e => console.error(e));
        }
        function updateUI(data) {
            document.getElementById('loader').innerText = "Status: Online | Target IP: 192.168.4.1";
            
            const isGame = data.mode === 0;
            document.getElementById('mode-badge').innerText = isGame ? "🎮 いろさがしゲーム" : "💡 ライトモード";
            document.getElementById('mode-badge').style.color = isGame ? "#00ffc8" : "#ffb800";
            document.getElementById('mode-badge').style.borderColor = isGame ? "rgba(0,255,200,0.3)" : "rgba(255,184,0,0.3)";
            document.getElementById('mode-badge').style.background = isGame ? "rgba(0,255,200,0.1)" : "rgba(255,184,0,0.1)";
            
            document.getElementById('game-view').style.display = isGame ? "block" : "none";
            document.getElementById('light-view').style.display = !isGame ? "block" : "none";

            if (isGame) {
                const trgb = hsvToRgb(data.targetHue, 1, 1);
                document.getElementById('target-color').style.background = `rgb(${trgb[0]},${trgb[1]},${trgb[2]})`;
                document.getElementById('target-color').innerText = `Hue: ${data.targetHue}°`;
                
                document.getElementById('pick-count').innerText = data.pickCountGame;
                
                [1, 2].forEach(i => {
                    const el = document.getElementById(`picked-${i}`);
                    if (i <= data.pickCountGame) {
                        const c = data.pickedColorsGame[i-1];
                        el.style.background = `rgb(${c.r},${c.g},${c.b})`;
                        el.innerText = `Hue: ${Math.round(c.h)}°`;
                    } else {
                        el.style.background = "rgba(0,0,0,0.3)";
                        el.innerText = "Empty";
                    }
                });

                const resSec = document.getElementById('result-section');
                if (data.showGameResult) {
                    resSec.style.display = "block";
                    const rs = document.getElementById('result-status');
                    rs.className = "status-text " + (data.lastGameResult ? "success" : "failure");
                    rs.innerText = data.lastGameResult ? "せいかい！！ 🎉" : "ざんねん… 😢";
                    
                    document.getElementById('diff-val').innerText = data.lastGameResultDiff.toFixed(1);
                    let targetRyb = rgbToRyb(data.targetHue);
                    let mixRyb = rgbToRyb(data.lastGameResultMixHue);
                    document.getElementById('marker-target').style.left = `calc(${(targetRyb / 360) * 100}% - 2px)`;
                    document.getElementById('marker-picked').style.left = `calc(${(mixRyb / 360) * 100}% - 2px)`;
                } else {
                    resSec.style.display = "none";
                }
            } else {
                [1, 2, 3].forEach(i => {
                    const el = document.getElementById(`light-${i}`);
                    if (i <= data.pickCountLight) {
                        const c = data.pickedColorsLight[i-1];
                        el.style.background = `rgb(${c.r},${c.g},${c.b})`;
                        el.innerText = ``;
                    } else {
                        el.style.background = "rgba(0,0,0,0.3)";
                        el.innerText = "Empty";
                    }
                });
                if (data.isRainbowMode) {
                    document.getElementById('rainbow-status').style.display = "block";
                    document.getElementById('btn-trigger-rainbow').style.display = "none";
                } else {
                    document.getElementById('rainbow-status').style.display = "none";
                    document.getElementById('btn-trigger-rainbow').style.display = "block";
                }
            }
        }

        setInterval(() => {
            fetch('/api/state')
                .then(r => r.json())
                .then(updateUI)
                .catch(e => {
                    console.error(e);
                    document.getElementById('loader').innerText = "Connection lost... Retrying";
                });
        }, 500);
    </script>
</body>
</html>
)rawliteral";

#endif
