#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include "httplib.h"

struct ScanNode {
    uintptr_t address;
    int lastValue;
};

std::vector<ScanNode> scanResults;
HANDLE currentProcessHandle = NULL;
DWORD currentPID = 0;

std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << val;
    return ss.str();
}

// ARAYÜZ: Yenilenmiş İşlem Listesi ve Arama
std::string get_ui() {
    std::string html = "<html><head><meta charset='UTF-8'><title>Luna Pro V8</title>";
    html += "<style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;padding:20px;}";
    html += ".panel{background:#161b22;padding:20px;border-radius:10px;border:1px solid #30363d;margin-bottom:15px;}";
    html += "input,select,button{background:#0d1117;border:1px solid #30363d;color:#f0f6fc;padding:10px;border-radius:5px;}";
    html += "button{background:#238636;cursor:pointer;border:none;font-weight:bold;margin:2px;} button:hover{opacity:0.8;}";
    html += ".btn-blue{background:#1f6feb;} .btn-red{background:#da3633;} .btn-purple{background:#8957e5;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:10px;} th,td{padding:10px;border-bottom:1px solid #30363d;text-align:left;}";
    html += "#procSearch{width:100%; box-sizing:border-box; margin-bottom:10px; border-color:#1f6feb;}";
    html += "</style></head><body>";
    
    html += "<div class='panel'><h2>LUNA PRO V8</h2>";
    html += "<input type='text' id='procSearch' placeholder='İşlem adını yazın (örn: roblox)...' oninput='updateProcList()'>";
    html += "<select id='procList' style='width:100%; margin-bottom:10px;' size='10'></select>";
    html += "<button onclick='attach()' style='width:100%'>SEÇİLİ İŞLEME BAĞLAN</button></div>";
    
    html += "<div class='panel'><h3>BELLEK MOTORU</h3>";
    html += "<input type='number' id='scanVal' placeholder='Değer'> ";
    html += "<button onclick=\"doScan('first')\">İLK TARAMA</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('next')\">FİLTRELE</button> ";
    html += "<button class='btn-red' onclick=\"doScan('decreased')\">AZALDI</button>";
    html += "<div style='margin-top:10px; color:#58a6ff;'>Bulunan: <span id='count'>0</span></div></div>";
    
    html += "<div class='panel'><table><thead><tr><th>ADRES</th><th>DEĞER</th><th>İŞLEM</th></tr></thead><tbody id='resTable'></tbody></table></div>";

    html += "<script>";
    html += "var allProcs = [];";
    
    // İşlemleri API'den Çek ve Sakla
    html += "function loadProcs(){ fetch('/api/procs').then(r=>r.json()).then(data=>{ allProcs = data; updateProcList(); }); }";
    
    // Listeyi Arama Kutusuna Göre Güncelle
    html += "function updateProcList(){ var term = document.getElementById('procSearch').value.toLowerCase();";
    html += "var s = document.getElementById('procList'); s.innerHTML = '';";
    html += "var filtered = allProcs.filter(p => p.name.toLowerCase().includes(term));";
    html += "filtered.forEach(p => { var o = document.createElement('option'); o.value = p.id; o.text = p.name + ' [' + p.id + ']'; s.appendChild(o); }); }";
    
    html += "function attach(){ var pid = document.getElementById('procList').value; if(!pid){alert('Lütfen bir işlem seçin!'); return;}";
    html += "fetch('/api/attach?pid='+pid).then(r=>r.text()).then(t=>alert(t)); }";
    
    html += "function doScan(mode){ var v = document.getElementById('scanVal').value;";
    html += "fetch('/api/scan?mode='+mode+'&val='+v).then(r=>r.json()).then(data=>{ renderTable(data.results); document.getElementById('count').innerText = data.total; }); }";
    
    html += "function renderTable(list){ var b = document.getElementById('resTable'); b.innerHTML = '';";
    html += "list.forEach(row => { b.innerHTML += '<tr><td>'+row.address+'</td><td><b>'+row.value+'</b></td>' + ";
    html += "'<td><button class=\"btn-blue\" onclick=\"writeVal(\\''+row.address+'\\')\">YAZ</button></td></tr>'; }); }";
    
    html += "function writeVal(addr){ var v = prompt('Yeni Değer:'); if(v) fetch('/api/write?addr='+addr+'&val='+v).then(()=>alert('Yazıldı')); }";
    
    html += "window.onload = loadProcs;";
    html += "</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    // API: İşlem Listesini Çekme (Hata Giderilmiş Sürüm)
    svr.Get("/api/procs", [](const httplib::Request&, httplib::Response& res) {
        std::string j = "[";
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (h != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
            if(Process32First(h, &pe)) {
                bool first = true;
                do {
                    std::string pName = pe.szExeFile;
                    if(pName == "System" || pName == "Idle") continue;
                    if(!first) j += ",";
                    j += "{\"id\":" + std::to_string(pe.th32ProcessID) + ",\"name\":\"" + pName + "\"}";
                    first = false;
                } while(Process32Next(h, &pe));
            }
            CloseHandle(h);
        }
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/api/attach", [](const httplib::Request& req, httplib::Response& res) {
        if(!req.has_param("pid")) return;
        currentPID = std::stoul(req.get_param_value("pid"));
        if(currentProcessHandle) CloseHandle(currentProcessHandle);
        currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
        res.set_content(currentProcessHandle ? "Bağlantı Başarılı!" : "Hata: Yönetici olarak çalıştırın!", "text/plain");
    });

    svr.Get("/api/scan", [](const httplib::Request& req, httplib::Response& res) {
        if(!currentProcessHandle) { res.set_content("{\"total\":0,\"results\":[]}", "application/json"); return; }
        std::string mode = req.get_param_value("mode");
        
        if(mode == "first") {
            scanResults.clear();
            int target = req.has_param("val") ? std::stoi(req.get_param_value("val")) : 0;
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            while(VirtualQueryEx(currentProcessHandle, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                if(mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE) {
                    std::vector<int> buffer(mbi.RegionSize / 4);
                    if(ReadProcessMemory(currentProcessHandle, mbi.BaseAddress, buffer.data(), mbi.RegionSize, NULL)) {
                        for(size_t i=0; i < buffer.size(); i++) {
                            if(buffer[i] == target) scanResults.push_back({ (uintptr_t)mbi.BaseAddress + (i * 4), buffer[i] });
                        }
                    }
                }
                addr += mbi.RegionSize;
                if(addr > 0x7FFFFFFF || scanResults.size() > 5000) break;
            }
        } else if(mode == "decreased" || mode == "next") {
            std::vector<ScanNode> next;
            for(auto& n : scanResults) {
                int cur = 0;
                if(ReadProcessMemory(currentProcessHandle, (LPCVOID)n.address, &cur, 4, NULL)) {
                    if((mode == "decreased" && cur < n.lastValue) || (mode == "next" && cur == std::stoi(req.get_param_value("val")))) {
                        n.lastValue = cur;
                        next.push_back(n);
                    }
                }
            }
            scanResults = next;
        }

        std::string json = "{\"total\":" + std::to_string(scanResults.size()) + ", \"results\":[";
        for(size_t i=0; i < (scanResults.size() > 30 ? 30 : scanResults.size()); i++) {
            int v = 0; ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i].address, &v, 4, NULL);
            if(i > 0) json += ",";
            json += "{\"address\":\"" + ToHex(scanResults[i].address) + "\",\"value\":" + std::to_string(v) + "}";
        }
        res.set_content(json + "]}", "application/json");
    });

    svr.Get("/api/write", [](const httplib::Request& req, httplib::Response& res) {
        uintptr_t a = std::stoull(req.get_param_value("addr"), nullptr, 16);
        int v = std::stoi(req.get_param_value("val"));
        WriteProcessMemory(currentProcessHandle, (LPVOID)a, &v, sizeof(v), NULL);
        res.set_content("Ok", "text/plain");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(get_ui(), "text/html"); });
    svr.listen("0.0.0.0", 1337);
    return 0;
}
