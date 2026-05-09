#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>

//#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"

// Küresel Veriler
std::vector<uintptr_t> scanResults;
HANDLE currentProcessHandle = NULL;
DWORD currentPID = 0;

// Yardımcı Fonksiyonlar
std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << std::uppercase << std::hex << val;
    return ss.str();
}

// AOB Tarama Motoru
bool CompareBytes(const unsigned char* data, const char* pattern, const char* mask) {
    for (; *mask; ++mask, ++data, ++pattern) {
        if (*mask == 'x' && *data != (unsigned char)*pattern) return false;
    }
    return (*mask == 0);
}

// Gelişmiş Web Arayüzü (Daha koyu, daha profesyonel)
const char* html_ui = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Luna Stealth Pro</title>
    <style>
        :root { --bg: #0b0f19; --card: #161d2f; --accent: #38bdf8; --text: #f1f5f9; --red: #ef4444; }
        body { background: var(--bg); color: var(--text); font-family: 'Consolas', monospace; margin: 0; display: flex; flex-direction: column; height: 100vh; }
        nav { background: var(--card); padding: 15px 30px; border-bottom: 1px solid #1e293b; display: flex; justify-content: space-between; align-items: center; }
        .main-grid { display: grid; grid-template-columns: 350px 1fr; gap: 20px; padding: 20px; flex: 1; overflow: hidden; }
        .panel { background: var(--card); border-radius: 12px; padding: 20px; border: 1px solid #1e293b; display: flex; flex-direction: column; }
        input, select, button { background: #0b0f19; border: 1px solid #334155; color: white; padding: 10px; border-radius: 6px; margin-bottom: 10px; }
        button { background: var(--accent); color: #000; font-weight: bold; cursor: pointer; border: none; }
        button:hover { filter: brightness(1.2); }
        .results-table { flex: 1; overflow-y: auto; background: #0b0f19; border-radius: 6px; }
        table { width: 100%; border-collapse: collapse; }
        th { position: sticky; top: 0; background: #1e293b; color: var(--accent); text-align: left; padding: 10px; }
        td { padding: 8px 10px; border-bottom: 1px solid #1e293b; font-size: 13px; }
        .row-selected { background: #38bdf822 !important; }
        .hex-view { font-family: 'Courier New'; font-size: 12px; color: #94a3b8; }
    </style>
</head>
<body>
    <nav>
        <div style="font-size: 20px; font-weight: bold; color: var(--accent);">LUNA STEALTH PRO <small style="color:white; font-size:10px;">v2.0</small></div>
        <div id="status">Sistem Beklemede</div>
    </nav>
    <div class="main-grid">
        <div class="panel">
            <h3>KONTROL MERKEZİ</h3>
            <label>Süreç Seçimi:</label>
            <select id="procList" onchange="attach()"><option>Yükleniyor...</option></select>
            
            <hr style="width:100%; border:0; border-top:1px solid #334155;">
            
            <label>Tarama Tipi (4-Byte Int):</label>
            <input type="number" id="scanVal" placeholder="Değer girin...">
            <div style="display:grid; grid-template-columns: 1fr 1fr; gap:10px;">
                <button onclick="doScan('first')">İLK TARAMA</button>
                <button onclick="doScan('next')">FİLTRELE</button>
            </div>
            
            <label>AOB Tarama (Byte Dizisi):</label>
            <input type="text" id="aobPattern" placeholder="48 8B 05 ?? ?? ??">
            <button onclick="aobScan()" style="background:#8b5cf6;">SİGNA SCAN</button>
        </div>

        <div class="panel">
            <h3>BELLEK SONUÇLARI <span id="resCount" style="font-size:12px; color:gray;"></span></h3>
            <div class="results-table">
                <table id="resTable">
                    <thead><tr><th>ADRES</th><th>DEĞER</th><th>DÖNÜŞÜM</th></tr></thead>
                    <tbody></tbody>
                </table>
            </div>
            <div style="margin-top:20px; display:flex; gap:10px; align-items:end;">
                <div style="flex:1">
                    <label>Seçili Adres:</label><input type="text" id="targetAddr" readonly>
                </div>
                <div style="flex:1">
                    <label>Yeni Değer:</label><input type="number" id="newVal">
                </div>
                <button onclick="writeMem()" style="height:38px; background:var(--red); color:white;">YAZ</button>
            </div>
        </div>
    </div>

    <script>
        function attach() {
            let pid = document.getElementById('procList').value;
            fetch('/api/attach?pid=' + pid).then(r => r.text()).then(t => document.getElementById('status').innerText = t);
        }

        function doScan(mode) {
            let val = document.getElementById('scanVal').value;
            fetch(`/api/scan?mode=${mode}&val=${val}`).then(r => r.json()).then(data => updateTable(data));
        }

        function writeMem() {
            let addr = document.getElementById('targetAddr').value;
            let val = document.getElementById('newVal').value;
            fetch(`/api/write?addr=${addr}&val=${val}`).then(r => r.text()).then(t => alert(t));
        }

        function updateTable(data) {
            let tbody = document.querySelector('#resTable tbody');
            tbody.innerHTML = '';
            document.getElementById('resCount').innerText = `(${data.length} sonuç)`;
            data.forEach(row => {
                let tr = document.createElement('tr');
                tr.onclick = () => { document.getElementById('targetAddr').value = row.address; };
                tr.innerHTML = `<td>${row.address}</td><td>${row.value}</td><td class="hex-view">${row.hex}</td>`;
                tbody.appendChild(tr);
            });
        }

        fetch('/api/procs').then(r => r.json()).then(data => {
            let s = document.getElementById('procList'); s.innerHTML = '';
            data.forEach(p => s.innerHTML += `<option value="${p.id}">${p.name}</option>`);
        });
    </script>
</body>
</html>
)";

// API Yönlendirmeleri ve Mantık
int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    httplib::Server svr;

    svr.Get("/", [](const auto&, auto& res) { res.set_content(html_ui, "text/html"); });

    svr.Get("/api/procs", [](const auto&, auto& res) {
        std::string j = "[";
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
        if(Process32First(h, &pe)) {
            do { j += (j=="["?"":",") + std::string("{\"id\":") + std::to_string(pe.th32ProcessID) + ",\"name\":\"" + pe.szExeFile + "\"}"; } while(Process32Next(h, &pe));
        }
        CloseHandle(h);
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/api/attach", [](const auto& req, auto& res) {
        currentPID = std::stoi(req.get_param_value("pid"));
        if(currentProcessHandle) CloseHandle(currentProcessHandle);
        currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
        res.set_content(currentProcessHandle ? "Bağlandı (PID: " + std::to_string(currentPID) + ")" : "HATA: Erişim Reddedildi", "text/plain");
    });

    svr.Get("/api/scan", [](const auto& req, auto& res) {
        std::string mode = req.get_param_value("mode");
        int target = std::stoi(req.get_param_value("val"));
        
        if(mode == "first") {
            scanResults.clear();
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            while(VirtualQueryEx(currentProcessHandle, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                if(mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE)) {
                    std::vector<int> buffer(mbi.RegionSize / 4);
                    if(ReadProcessMemory(currentProcessHandle, mbi.BaseAddress, buffer.data(), mbi.RegionSize, NULL)) {
                        for(size_t i=0; i<buffer.size(); i++) if(buffer[i] == target) scanResults.push_back((uintptr_t)mbi.BaseAddress + (i*4));
                    }
                }
                addr += mbi.RegionSize;
            }
        } else {
            std::vector<uintptr_t> filtered;
            for(auto a : scanResults) {
                int v; ReadProcessMemory(currentProcessHandle, (LPCVOID)a, &v, 4, NULL);
                if(v == target) filtered.push_back(a);
            }
            scanResults = filtered;
        }

        std::string j = "[";
        for(size_t i=0; i < (scanResults.size() > 200 ? 200 : scanResults.size()); i++) {
            int v; ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i], &v, 4, NULL);
            j += (i==0?"":",") + std::string("{\"address\":\"") + ToHex(scanResults[i]) + "\",\"value\":" + std::to_string(v) + ",\"hex\":\"" + ToHex(v) + "\"}";
        }
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/api/write", [](const auto& req, auto& res) {
        uintptr_t addr = std::stoull(req.get_param_value("addr"), nullptr, 16);
        int val = std::stoi(req.get_param_value("val"));
        bool s = WriteProcessMemory(currentProcessHandle, (LPVOID)addr, &val, 4, NULL);
        res.set_content(s ? "Yazıldı" : "Hata", "text/plain");
    });

    svr.listen("127.0.0.1", 1337);
    return 0;
}

