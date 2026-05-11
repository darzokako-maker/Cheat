import cv2
import numpy as np
import pyautogui
from flask import Flask, Response, request, render_template_string
import mss
import time

pyautogui.FAILSAFE = False
app = Flask(__name__)
sct = mss.mss()
monitor = sct.monitors[1]

def get_screen_stream():
    last_time = 0
    while True:
        # FPS Sınırla: Saniyede max 20 kare (Gecikmeyi önlemek için kritik)
        if (time.time() - last_time) < 0.05: 
            continue
        last_time = time.time()

        sct_img = sct.grab(monitor)
        img = np.array(sct_img)
        frame = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        
        # PERFORMANS AYARI: 
        # fx=0.3 görüntüyü %30'a düşürür. Telefondan kontrol için idealdir.
        frame = cv2.resize(frame, (0, 0), fx=0.3, fy=0.3)
        
        # JPEG KALİTESİ: 
        # 40-50 arası akıcılığı artırır, görüntüdeki bozulma telefonda çok belli olmaz.
        ret, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 45])
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + buffer.tobytes() + b'\r\n')

@app.route('/')
def index():
    return render_template_string('''
    <!DOCTYPE html>
    <html>
    <head>
        <title>Luna Mobile Remote</title>
        <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
        <style>
            body { background: #000; margin: 0; padding: 0; display: flex; flex-direction: column; height: 100vh; overflow: hidden; }
            #container { width: 100%; flex-grow: 1; position: relative; background: #050505; display: flex; align-items: center; justify-content: center; }
            #screen { width: 100%; height: auto; max-height: 100%; object-fit: contain; -webkit-user-select: none; }
            #status { background: #111; color: #0f0; font-family: monospace; font-size: 12px; padding: 5px; text-align: center; }
        </style>
    </head>
    <body>
        <div id="status">MOBİL OPTİMİZE MOD: AKTİF</div>
        <div id="container">
            <img id="screen" src="/video_feed">
        </div>
        <script>
            const screen = document.getElementById('screen');
            function sendCommand(action, e) {
                const rect = screen.getBoundingClientRect();
                // Dokunmatik koordinatları hesapla
                const clientX = e.clientX || (e.touches && e.touches[0].clientX);
                const clientY = e.clientY || (e.touches && e.touches[0].clientY);
                
                const x = (clientX - rect.left) / rect.width;
                const y = (clientY - rect.top) / rect.height;
                fetch(`/remote_input?type=${action}&x=${x}&y=${y}`);
            }

            // Mobil için dokunmatik desteği
            screen.addEventListener('touchstart', (e) => {
                sendCommand('left_click', e);
                // e.preventDefault(); // Sayfanın kaymasını engeller
            }, {passive: false});

            // Sağ tık simülasyonu için uzun basma veya ekranın üst kısmına özel alan eklenebilir
            screen.addEventListener('contextmenu', (e) => e.preventDefault());
        </script>
    </body>
    </html>
    ''')

@app.route('/video_feed')
def video_feed():
    return Response(get_screen_stream(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/remote_input')
def remote_input():
    # Mouse komutları aynı kalıyor
    ix = request.args.get('type')
    rx = float(request.args.get('x'))
    ry = float(request.args.get('y'))
    sw, sh = pyautogui.size()
    tx, ty = int(rx * sw), int(ry * sh)
    
    if ix == 'left_click':
        pyautogui.click(tx, ty)
    return "OK"

if __name__ == '__main__':
    # threaded=True bağlantıların birbirini kilitmemesini sağlar
    app.run(host='0.0.0.0', port=5000, threaded=True)
    
