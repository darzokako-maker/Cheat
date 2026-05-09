#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include "httplib.h"

// Bellek düğümü: Adres ve son bilinen değer
struct ScanNode {
    uintptr_t address;
    int lastValue;
};

std::vector<ScanNode> scanResults;
HANDLE currentProcessHandle = NULL;
DWORD currentPID = 0;

// Adresi Hex formatına çevirir (Görsel kolaylık için)
std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << val;
    return ss.str();
}

// ARAYÜZ TASARIMI
std::string get_ui() {
    std::string html = "<html><head><meta charset='UTF-8'><title>Luna Pro V9</title>";
    html += "<style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;padding:20px;}";
    html += ".panel{background:#161b22;padding:20px;border-radius:10px;border:1px solid #30363d;margin-bottom:15px;}";
    html += "input,select,button{background:#0d1117;border:1px solid #30363d;color:#f0f6fc;padding:10px;border-radius:5px;}";
    html += "button{background:#238636;cursor:pointer;border:none;font-weight:bold;margin:2px;min-width:90px;} button:hover{opacity:0.8;}";
    html += ".btn-blue{background:#1f6feb;} .btn-red{background:#da3633;} .btn-purple{background:#8957e5;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:10px;} th,td{padding:10px;border-bottom:1px solid #30363d;text-align:left;}";
    html += "#procSearch{width:100%; box-sizing:border-box; margin-bottom:10px; border-color:#1f6feb;}";
    html += ".status{color:#58a6ff; font-weight:bold; margin-top:5px;}</style></head><body>";
    
    html += "<div class='panel'><h2>LUNA PRO V9 - KONTROL MERKEZİ</h2>";
    html += "<input type='text' id='procSearch' placeholder='İşlem Ara (örn: roblox, notepad)...' oninput='updateProcList()'>";
    html += "<select id='procList' style='width:100%;' size='6'></select>";
    html += "<button onclick='attach()' style='width:100%; margin-top:10px;' class='btn-blue'>İŞLEME BAĞLAN</button></div>";
    
    html += "<div class='panel'><h3>ARAMA VE FİLTRELEME</h3>";
    html += "<input type='number' id='scanVal' placeholder='Sayısal Değer (Opsiyonel)'> ";
    html += "<button onclick=\"doScan('first')\">İLK TARAMA</button> ";
    html += "<button onclick=\"doScan('unknown')\" style='background:#484f58'>BİLİNMEYEN BAŞLAT</button><hr style='border:0; border-top:1px solid #30363d; margin:10px 0;'>";
    
    // İstediğin Butonlar: Arttı, Azaldı, Değişmedi
    html += "<button class='btn-purple' onclick=\"doScan('increased')\">ARTTI (+)</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('decreased')\">AZALDI (-)</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('unchanged')\">DEĞİŞMEDİ (=)</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('changed')\">DEĞİŞTİ (!)</button>";
    html += "<div class='status'>Bulunan Adres: <span id='count'>0</span></div></div>";
    
    html += "<div class='panel'><table><thead><tr><th>ADRES</th><th>GÜNCEL DEĞER</th><th>ÖNCEKİ</th><th>AKSİYON</th></tr></thead><tbody id='resTable'></tbody></table>";
    html += "<button onclick='massWrite()' class='btn-blue' style='margin-top:10px; width:100%;'>LİSTEDEKİLERİ TOPLU DEĞİŞTİR</button></div>";

    html += "<script>";
    html += "var allProcs = []; function loadProcs(){ fetch('/api/procs').then(r=>r.json()).then(data=>{ allProcs=data; updateProcList(); }); }";
    html += "function updateProcList(){ var term=document.getElementById('procSearch').value.toLowerCase(); var s=document.getElementById('procList'); s.innerHTML=''; allProcs.filter(p=>p.name.toLowerCase().includes(term)).forEach(p=>{ var o=document.createElement('option'); o.value=p.id; o.text=p.name+' ['+p.id+']'; s.appendChild(o); }); }";
    html += "function attach(){ var pid=document.getElementById('procList').value; if(!pid){alert('Lütfen listeden bir işlem seçin!'); return;} fetch('/api/attach?pid='+pid).then(r=>r.text()).then(t=>alert(t)); }";
    html += "function doScan(mode){ var v=document.getElementById('scanVal').value; fetch('/api/scan?mode='+mode+'&val='+v).then(r=>r.json()).then(data=>{ renderTable(data.results); document.getElementById('count').innerText=data.total; }); }";
    html += "function renderTable(list){ var b=document.getElementById('resTable'); b.innerHTML=''; list.forEach(row=>{ b.innerHTML+='<tr><td>'+row.address+'</td><td><b>'+row.value+'</b></td><td>'+row.prev+'</td><td><button class=\"btn-blue\" onclick=\"writeVal(\\''+row.address+'\\')\">DEĞİŞTİR</button></td></tr>'; }); }";
    html += "function writeVal(addr){ var v=prompt('Yeni Değer:'); if(v) fetch('/api/write?addr='+addr+'&val='+v).then(()=>doScan('refresh')); }";
    html += "function massWrite(){ var v=prompt('Tüm listedeki adreslere yazılacak değer:'); if(v) fetch('/api/masswrite?val='+v).then(()=>doScan('refresh')); }";
    html += "window.onload=loadProcs;";
    html += "</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    // SİSTEMDEKİ İŞLEMLERİ ÇEKER
    svr.Get("/api/procs", [](const httplib::Request&, httplib::Response& res) {
        std::string j = "[";
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if(h != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
            if(Process32First(h, &pe)) {
                bool first = true;
                do {
                    if(!first) j += ",";
                    j += "{\"id\":" + std::to_string(pe.th32ProcessID) + ",\"name\":\"" + std::string(pe.szExeFile) + "\"}";
                    first = false;
                } while(Process32Next(h, &pe));
            }
            CloseHandle(h);
        }
        res.set_content(j + "]", "application/json");
    });

    // SEÇİLEN OYUNA BAĞLANIR
    svr.Get("/api/attach", [](const httplib::Request& req, httplib::Response& res) {
        currentPID = std::stoul(req.get_param_value("pid"));
        if(currentProcessHandle) CloseHandle(currentProcessHandle);
        currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
        res.set_content(currentProcessHandle ? "Bağlantı Başarılı!" : "Hata: Erişim Engellendi!", "text/plain");
    });

    // BELLEK TARAMA MOTORU (ARTTI, AZALDI, DEĞİŞMEDİ MANTIĞI)
    svr.Get("/api/scan", [](const httplib::Request& req, httplib::Response& res) {
        if(!currentProcessHandle) return;
        std::string mode = req.get_param_value("mode");
        
        if(mode == "first" || mode == "unknown") {
            scanResults.clear();
            int target = (mode == "first" && req.has_param("val") && !req.get_param_value("val").empty()) ? std::stoi(req.get_param_value("val")) : 0;
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            while(VirtualQueryEx(currentProcessHandle, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                if(mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE) {
                    std::vector<int> buffer(mbi.RegionSize / 4);
                    if(ReadProcessMemory(currentProcessHandle, mbi.BaseAddress, buffer.data(), mbi.RegionSize, NULL)) {
                        for(size_t i=0; i < buffer.size(); i++) {
                            if(mode == "unknown" || buffer[i] == target)
                                scanResults.push_back({ (uintptr_t)mbi.BaseAddress + (i * 4), buffer[i] });
                        }
                    }
                }
                addr += mbi.RegionSize;
                if(addr > 0x7FFFFFFF || scanResults.size() > 50000) break;
            }
        } else {
            // FİLTRELEME İŞLEMLERİ
            std::vector<ScanNode> next;
            for(auto& n : scanResults) {
                int cur = 0;
                if(ReadProcessMemory(currentProcessHandle, (LPCVOID)n.address, &cur, 4, NULL)) {
                    bool match = false;
                    if(mode == "increased") match = (cur > n.lastValue);
                    else if(mode == "decreased") match = (cur < n.lastValue);
                    else if(mode == "unchanged") match = (cur == n.lastValue);
                    else if(mode == "changed") match = (cur != n.lastValue);
                    else if(mode == "refresh") match = true;
                    
                    if(match) { n.lastValue = cur; next.push_back(n); }
                }
            }
            scanResults = next;
        }

        // SONUÇLARI JSON OLARAK GÖNDER
        std::string json = "{\"total\":" + std::to_string(scanResults.size()) + ",\"results\":[";
        for(size_t i=0; i < (scanResults.size() > 50 ? 50 : scanResults.size()); i++) {
            int v = 0; ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i].address, &v, 4, NULL);
            if(i > 0) json += ",";
            json += "{\"address\":\"" + ToHex(scanResults[i].address) + "\",\"value\":" + std::to_string(v) + ",\"prev\":" + std::to_string(scanResults[i].lastValue) + "}";
        }
        res.set_content(json + "]}", "application/json");
    });

    // TEKLİ VE TOPLU DEĞER YAZMA
    svr.Get("/api/write", [](const httplib::Request& req, httplib::Response& res) {
        uintptr_t a = std::stoull(req.get_param_value("addr"), nullptr, 16);
        int v = std::stoi(req.get_param_value("val"));
        WriteProcessMemory(currentProcessHandle, (LPVOID)a, &v, sizeof(v), NULL);
        res.set_content("Ok", "text/plain");
    });

    svr.Get("/api/masswrite", [](const httplib::Request& req, httplib::Response& res) {
        int v = std::stoi(req.get_param_value("val"));
        for(auto& n : scanResults) WriteProcessMemory(currentProcessHandle, (LPVOID)n.address, &v, sizeof(v), NULL);
        res.set_content("Ok", "text/plain");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(get_ui(), "text/html"); });
    svr.listen("0.0.0.0", 1337);
    return 0;
}
