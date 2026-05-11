import cv2
import numpy as np
import pyautogui
from flask import Flask, Response, request, render_template_string
import mss
import time

# Mouse hata korumasını kapat
pyautogui.FAILSAFE = False
# Mouse hareket hızını sıfıra çek (Anında tepki için)
pyautogui.PAUSE = 0

app = Flask(__name__)
sct = mss.mss()
monitor = sct.monitors[1]

def get_screen_stream():
    last_time = 0
    while True:
        # FPS SABİTLEME: Saniyede 15 kare akıcılık için yeterlidir ve gecikmeyi önler
        current_time = time.time()
        if (current_time - last_time) < 0.06: 
            continue
        last_time = current_time

        # Ekranı yakala
        sct_img = sct.grab(monitor)
        img = np.array(sct_img)
        frame = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        
        # MOBİL OPTİMİZASYON: 
        # fx=0.25 görüntüyü orijinalin 1/4'üne indirir. Telefondan akıcılık için en iyi ayardır.
        frame = cv2.resize(frame, (0, 0), fx=0.25, fy=0.25, interpolation=cv2.INTER_AREA)
        
        # ULTRA SIKIŞTIRMA: 
        # Kaliteyi 35'e düşürerek veri boyutunu %90 oranında azalttık.
        ret, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 35])
        
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + buffer.tobytes() + b'\r\n')

@app.route('/')
def index():
    return render_template_string('''
    <!DOCTYPE html>
    <html>
    <head>
        <title>Luna Ultra Fluid</title>
        <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
        <style>
            body { background: #000; margin: 0; padding: 0; height: 100vh; display: flex; flex-direction: column; overflow: hidden; }
            #container { flex: 1; position: relative; background: #000; display: flex; align-items: center; justify-content: center; }
            #screen { width: 100%; height: auto; max-height: 100vh; object-fit: contain; }
            #info { position: fixed; top: 0; width: 100%; background: rgba(0,255,0,0.1); color: #0f0; font-size: 10px; text-align: center; z-index: 10; }
        </style>
    </head>
    <body>
        <div id="info">ULTRA FLUID MODE | LAG REDUCTION ACTIVE</div>
        <div id="container">
            <img id="screen" src="/video_feed">
        </div>
        <script>
            const screen = document.getElementById('screen');
            
            function sendCommand(action, e) {
                const rect = screen.getBoundingClientRect();
                const clientX = e.touches ? e.touches[0].clientX : e.clientX;
                const clientY = e.touches ? e.touches[0].clientY : e.clientY;
                
                const x = (clientX - rect.left) / rect.width;
                const y = (clientY - rect.top) / rect.height;
                
                // Gecikmeyi azaltmak için arka planda hızlıca gönder (No-wait)
                navigator.sendBeacon(`/remote_input?type=${action}&x=${x}&y=${y}`);
            }

            screen.addEventListener('touchstart', (e) => {
                sendCommand('click', e);
            }, {passive: true});

            screen.addEventListener('contextmenu', (e) => e.preventDefault());
        </script>
    </body>
    </html>
    ''')

@app.route('/remote_input')
def remote_input():
    # Hızlı komut işleme
    t = request.args.get('type')
    x = float(request.args.get('x'))
    y = float(request.args.get('y'))
    w, h = pyautogui.size()
    
    if t == 'click':
        pyautogui.click(int(x * w), int(y * h))
    return "", 204

if __name__ == '__main__':
    # threaded=True ve 0.0.0.0 ile yerel ağda en yüksek performansı verir
    app.run(host='0.0.0.0', port=5000, threaded=True, debug=False)
    
