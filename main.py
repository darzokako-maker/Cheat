import pymem
import pymem.process
from flask import Flask, render_template_string, jsonify
import threading
import time
import random

app = Flask(__name__)

# 8 Mayıs 2026 - Build 14160 Dosyandan Alınan Offsetler
class Offsets:
    dwLocalPlayerPawn = 0x2057720
    dwEntityList = 0x24D1DF0
    m_iHealth = 0x334
    m_iTeamNum = 0x3CB
    m_vOldOrigin = 0x1324

shared_data = {"players": []}

def memory_worker():
    while True:
        try:
            # Sadece OKUMA yetkisiyle baglan (PROCESS_VM_READ)
            pm = pymem.Pymem("cs2.exe")
            client = pymem.process.module_from_name(pm.process_handle, "client.dll").lpBaseOfDll
            
            while True:
                temp_players = []
                local_pawn = pm.read_longlong(client + Offsets.dwLocalPlayerPawn)
                elist = pm.read_longlong(client + Offsets.dwEntityList)
                
                if not local_pawn or not elist: continue

                for i in range(1, 64):
                    try:
                        entry = pm.read_longlong(elist + (8 * (i & 0x7FFF) >> 9) + 16)
                        pawn = pm.read_longlong(entry + 120 * (i & 0x1FF))
                        if not pawn or pawn == local_pawn: continue
                        
                        # SADECE OKUMA YAPILIYOR (GIZLILIK ICIN EN IYISI)
                        hp = pm.read_int(pawn + Offsets.m_iHealth)
                        if 0 < hp <= 100:
                            x = pm.read_float(pawn + Offsets.m_vOldOrigin)
                            y = pm.read_float(pawn + Offsets.m_vOldOrigin + 4)
                            team = pm.read_int(pawn + Offsets.m_iTeamNum)
                            temp_players.append({"x": x, "y": y, "team": team})
                    except: continue
                
                shared_data["players"] = temp_players
                # Okuma hızını hafif dalgalı yaparak fark edilmeyi zorlastır
                time.sleep(random.uniform(0.05, 0.08))
                
        except:
            time.sleep(3)

@app.route('/')
def index():
    return render_template_string('''
        <body style="background:#000; color:#0f0; font-family:monospace; text-align:center;">
            <h3>GIZLI WEB MONITOR - V14160</h3>
            <div id="radar" style="width:500px; height:500px; border:2px solid #222; margin:auto; position:relative; background:#111; border-radius:50%; overflow:hidden;"></div>
            <script>
                function update() {
                    fetch('/data').then(r=>r.json()).then(d=>{
                        const r=document.getElementById('radar'); r.innerHTML='';
                        d.players.forEach(p=>{
                            const dot=document.createElement('div');
                            dot.style.cssText=`position:absolute; width:10px; height:10px; border-radius:50%; background:${p.team==2?'red':'blue'}; left:${p.x/15+250}px; top:${p.y/-15+250}px; border:1px solid white;`;
                            r.appendChild(dot);
                        });
                    });
                }
                setInterval(update, 100);
            </script>
        </body>
    ''')

@app.route('/data')
def data(): return jsonify(shared_data)

if __name__ == "__main__":
    threading.Thread(target=memory_worker, daemon=True).start()
    app.run(host='0.0.0.0', port=5000)
  
