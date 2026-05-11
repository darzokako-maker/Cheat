import cv2
import numpy as np
import pyautogui
from flask import Flask, Response, request, render_template_string
import mss

# Mouse hatalarını engellemek için güvenliği kapat
pyautogui.FAILSAFE = False

app = Flask(__name__)

# Ekran yakalama ayarı (Birinci monitör)
sct = mss.mss()
monitor = sct.monitors[1]

def get_screen_stream():
    while True:
        # Ekran görüntüsünü al
        sct_img = sct.grab(monitor)
        img = np.array(sct_img)
        
        # BGRA -> BGR çevirimi
        frame = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
        
        # Hız için görüntüyü %50 küçült (Daha az lag)
        frame = cv2.resize(frame, (0, 0), fx=0.5, fy=0.5)
        
        # JPEG olarak sıkıştır (Kalite 70)
        ret, buffer = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 70])
        frame_bytes = buffer.tobytes()
        
        # Web yayını formatında gönder
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + frame_bytes + b'\r\n')

@app.route('/')
def index():
    # Web arayüzü
    return render_template_string('''
    <!DOCTYPE html>
    <html>
    <head>
        <title>Luna Remote Desktop</title>
        <style>
            body { background: #000; color: #0f0; font-family: monospace; margin: 0; display: flex; flex-direction: column; align-items: center; overflow: hidden; }
            #status { padding: 10px; background: #111; width: 100%; text-align: center; border-bottom: 1px solid #333; }
            #container { position: relative; width: 95vw; height: 80vh; margin-top: 10px; border: 1px solid #444; background: #050505; }
            #screen { width: 100%; height: 100%; object-fit: contain; cursor: crosshair; }
        </style>
    </head>
    <body>
        <div id="status">LUNA REMOTE - CANLI YAYIN</div>
        <div id="container">
            <img id="screen" src="/video_feed">
        </div>
        <script>
            const screen = document.getElementById('screen');
            function sendCommand(action, e) {
                const rect = screen.getBoundingClientRect();
                const x = (e.clientX - rect.left) / rect.width;
                const y = (e.clientY - rect.top) / rect.height;
                fetch(`/remote_input?type=${action}&x=${x}&y=${y}`);
            }
            screen.addEventListener('mousedown', (e) => {
                if (e.button === 0) sendCommand('left_click', e);
            });
            screen.addEventListener('contextmenu', (e) => {
                e.preventDefault();
                sendCommand('right_click', e);
            });
        </script>
    </body>
    </html>
    ''')

@app.route('/video_feed')
def video_feed():
    return Response(get_screen_stream(), mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/remote_input')
def remote_input():
    input_type = request.args.get('type')
    rel_x = float(request.args.get('x'))
    rel_y = float(request.args.get('y'))
    
    # Gerçek ekran boyutlarını al
    screen_w, screen_h = pyautogui.size()
    target_x = int(rel_x * screen_w)
    target_y = int(rel_y * screen_h)
    
    if input_type == 'left_click':
        pyautogui.click(target_x, target_y)
    elif input_type == 'right_click':
        pyautogui.rightClick(target_x, target_y)
    return "OK"

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, threaded=True)
    
