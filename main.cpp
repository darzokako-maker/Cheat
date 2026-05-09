#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include "httplib.h"

// Her adresin son taranan değerini tutmak için yapı
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

std::string get_ui() {
    std::string html = "<html><head><meta charset='UTF-8'><title>Luna Pro V7</title>";
    html += "<style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;padding:20px;}";
    html += ".panel{background:#161b22;padding:20px;border-radius:10px;border:1px solid #30363d;margin-bottom:15px;}";
    html += "input,select,button{background:#0d1117;border:1px solid #30363d;color:#f0f6fc;padding:10px;border-radius:5px;}";
    html += "button{background:#238636;cursor:pointer;border:none;font-weight:bold;margin:2px;} button:hover{opacity:0.8;}";
    html += ".btn-blue{background:#1f6feb;} .btn-red{background:#da3633;} .btn-purple{background:#8957e5;} .btn-gray{background:#484f58;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:10px;} th,td{padding:10px;border-bottom:1px solid #30363d;}";
    html += ".status-bar{margin-top:10px;font-weight:bold;color:#58a6ff;}</style></head><body>";
    
    html += "<div class='panel'><h2>LUNA PRO V7 - UNKNOWN SEARCH</h2>";
    html += "<input type='text' id='procSearch' placeholder='İşlem Ara...' onkeyup='filterProcs()'> ";
    html += "<select id='procList' style='width:250px;'></select> <button onclick='attach()'>BAĞLAN</button></div>";
    
    html += "<div class='panel'><h3>ARAMA MOTORU</h3>";
    html += "<input type='number' id='scanVal' placeholder='Değer (Opsiyonel)'> ";
    html += "<button onclick=\"doScan('first')\">İLK TARAMA (BİLİNEN)</button> ";
    html += "<button class='btn-gray' onclick=\"doScan('unknown')\">İLK TARAMA (BİLİNMEYEN)</button> <hr>";
    
    // Bilinmeyen Değer Filtreleri
    html += "<div style='margin-top:10px;'>";
    html += "<button class='btn-purple' onclick=\"doScan('increased')\">ARTTI (+)</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('decreased')\">AZALDI (-)</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('changed')\">DEĞİŞTİ (!)</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('unchanged')\">DEĞİŞMEDİ (=)</button>";
    html += "</div><div class='status-bar'>Bulunan: <span id='count'>0</span></div></div>";
    
    html += "<div class='panel'><button class='btn-blue' onclick='massWrite()'>TOPLU YAZ</button> ";
    html += "<button class='btn-red' onclick='massDelete()'>TEMİZLE</button>";
    html += "<table><thead><tr><th>ADRES</th><th>GÜNCEL</th><th>ÖNCEKİ</th><th>İŞLEM</th></tr></thead><tbody id='resTable'></tbody></table></div>";

    html += "<script>";
    html += "function attach(){ fetch('/api/attach?pid='+document.getElementById('procList').value).then(r=>r.text()).then(t=>alert(t)); }";
    html += "function doScan(mode){ var v=document.getElementById('scanVal').value;";
    html += "fetch('/api/scan?mode='+mode+'&val='+v).then(r=>r.json()).then(data=>{ renderTable(data.results); document.getElementById('count').innerText=data.total; }); }";
    html += "function renderTable(list){ var b=document.getElementById('resTable'); b.innerHTML=''; list.forEach((row, index)=>{ b.innerHTML+='<tr><td>'+row.address+'</td><td><b>'+row.value+'</b></td><td>'+row.prev+'</td><td><button class=\"btn-blue\" onclick=\"writeVal(\\''+row.address+'\\')\">YAZ</button></td></tr>'; }); }";
    html += "function massWrite(){ var v=prompt('Toplu değer:'); if(v) fetch('/api/masswrite?val='+v).then(()=>doScan('refresh')); }";
    html += "function writeVal(addr){ var v=prompt('Yeni Değer:'); if(v) fetch('/api/write?addr='+addr+'&val='+v).then(()=>doScan('refresh')); }";
    html += "function massDelete(){ fetch('/api/massdelete').then(()=>location.reload()); }";
    // İşlem listesi yükleme kodu buraya...
    html += "fetch('/api/procs').then(r=>r.json()).then(data=>{ var s=document.getElementById('procList'); data.forEach(p=>{ var o=document.createElement('option'); o.value=p.id; o.text=p.name; s.appendChild(o); }); });";
    html += "</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    // ... (Procs ve Attach API'leri aynı)

    svr.Get("/api/scan", [](const httplib::Request& req, httplib::Response& res) {
        if(!currentProcessHandle) return;
        std::string mode = req.get_param_value("mode");
        
        if(mode == "first" || mode == "unknown") {
            scanResults.clear();
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            int target = (mode == "first") ? std::stoi(req.get_param_value("val")) : 0;

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
                if(addr > 0x7FFFFFFF || scanResults.size() > 100000) break; 
            }
        } else {
            // FİLTRELEME MODLARI (Artma, Azalma, Değişme)
            std::vector<ScanNode> nextResults;
            for(auto& node : scanResults) {
                int currentVal = 0;
                if(ReadProcessMemory(currentProcessHandle, (LPCVOID)node.address, &currentVal, 4, NULL)) {
                    bool match = false;
                    if(mode == "increased") match = (currentVal > node.lastValue);
                    else if(mode == "decreased") match = (currentVal < node.lastValue);
                    else if(mode == "changed") match = (currentVal != node.lastValue);
                    else if(mode == "unchanged") match = (currentVal == node.lastValue);
                    else if(mode == "refresh") match = true; // Sadece değerleri güncelle

                    if(match) {
                        node.lastValue = currentVal; // Gölge değeri güncelle
                        nextResults.push_back(node);
                    }
                }
            }
            scanResults = nextResults;
        }

        // JSON Yanıtı
        std::string json = "{\"total\":" + std::to_string(scanResults.size()) + ", \"results\":[";
        for(size_t i=0; i < (scanResults.size() > 50 ? 50 : scanResults.size()); i++) {
            int v = 0; ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i].address, &v, 4, NULL);
            if(i > 0) json += ",";
            json += "{\"address\":\"" + ToHex(scanResults[i].address) + "\",\"value\":" + std::to_string(v) + ",\"prev\":" + std::to_string(scanResults[i].lastValue) + "}";
        }
        json += "]}";
        res.set_content(json, "application/json");
    });

    // MassWrite ve Write API'leri buraya... (Öncekiyle aynı)
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(get_ui(), "text/html"); });
    svr.listen("127.0.0.1", 1337);
    return 0;
}
