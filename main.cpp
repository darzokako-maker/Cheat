#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include "httplib.h"

// Küresel Değişkenler
std::vector<uintptr_t> scanResults;
HANDLE currentProcessHandle = NULL;
DWORD currentPID = 0;

// Yardımcı Fonksiyon: Adresi Hex formatına çevirir
std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << val;
    return ss.str();
}

// Modern ve Kullanışlı Arayüz
std::string get_ui() {
    std::string html = "<html><head><meta charset=\"UTF-8\"><title>Luna Pro V4</title>";
    html += "<style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;padding:20px;}";
    html += ".panel{background:#161b22;padding:20px;border-radius:10px;border:1px solid #30363d;margin-bottom:15px;}";
    html += "input,select,button{background:#0d1117;border:1px solid #30363d;color:#f0f6fc;padding:10px;border-radius:5px;}";
    html += "button{background:#238636;cursor:pointer;border:none;font-weight:bold;} button:hover{background:#2ea043;}";
    html += ".btn-edit{background:#1f6feb; margin-right:5px;} .btn-del{background:#da3633;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:20px;} th,td{padding:12px;border-bottom:1px solid #30363d;text-align:left;}";
    html += "tr:hover{background:#1c2128;}</style></head><body>";
    
    html += "<div class=\"panel\"><h2>LUNA PRO V4</h2>";
    html += "<select id=\"procList\"></select> <button onclick=\"attach()\">SÜRECE BAĞLAN</button></div>";
    
    html += "<div class=\"panel\"><h3>BELLEK TARAYICI</h3>";
    html += "<input type=\"number\" id=\"scanVal\" placeholder=\"Aranacak Değer\"> ";
    html += "<button onclick=\"doScan('first')\">İLK TARAMA</button> ";
    html += "<button onclick=\"doScan('next')\" style=\"background:#8957e5\">FİLTRELE</button></div>";
    
    html += "<div class=\"panel\"><table><thead><tr><th>BELLEK ADRESİ</th><th>GÜNCEL DEĞER</th><th>AKSİYONLAR</th></tr></thead>";
    html += "<tbody id=\"resTable\"></tbody></table></div>";

    html += "<script>";
    html += "function attach(){fetch('/api/attach?pid='+document.getElementById('procList').value).then(r=>r.text()).then(t=>alert(t));}";
    
    html += "function doScan(mode){ var val = document.getElementById('scanVal').value;";
    html += "document.getElementById('resTable').innerHTML = '<tr><td colspan=\"3\">Tarama yapılıyor...</td></tr>';";
    html += "fetch('/api/scan?mode='+mode+'&val='+val).then(r=>r.json()).then(data=>{ renderTable(data); }); }";
    
    html += "function renderTable(data){ var b = document.getElementById('resTable'); b.innerHTML = '';";
    html += "data.forEach((row, index)=>{ b.innerHTML += '<tr><td>'+row.address+'</td><td><b>'+row.value+'</b></td>' +";
    html += "'<td><button class=\"btn-edit\" onclick=\"writeVal(\\''+row.address+'\\')\">DÜZENLE</button>' +";
    html += "'<button class=\"btn-del\" onclick=\"removeRow('+index+')\">SİL</button></td></tr>'; }); }";
    
    html += "function writeVal(addr){ var v = prompt(addr + ' adresi için yeni değer:');";
    html += "if(v) fetch('/api/write?addr='+addr+'&val='+v).then(r=>r.text()).then(t=>{ alert(t); doScan('refresh'); }); }";
    
    html += "function removeRow(idx){ fetch('/api/remove?idx='+idx).then(r=>r.json()).then(data=>renderTable(data)); }";
    
    html += "fetch('/api/procs').then(r=>r.json()).then(data=>{ var s = document.getElementById('procList');";
    html += "data.forEach(p=>{ var o = document.createElement('option'); o.value=p.id; o.text=p.name; s.appendChild(o); }); });";
    html += "</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    // API: Süreç Listesi
    svr.Get("/api/procs", [](const httplib::Request&, httplib::Response& res) {
        std::string j = "[";
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
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
        res.set_content(j + "]", "application/json");
    });

    // API: Bağlan
    svr.Get("/api/attach", [](const httplib::Request& req, httplib::Response& res) {
        currentPID = std::stoul(req.get_param_value("pid"));
        if(currentProcessHandle) CloseHandle(currentProcessHandle);
        currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
        res.set_content(currentProcessHandle ? "Baglandi!" : "Hata!", "text/plain");
    });

    // API: Tarama ve Liste Yenileme
    svr.Get("/api/scan", [](const httplib::Request& req, httplib::Response& res) {
        if(!currentProcessHandle) { res.set_content("[]", "application/json"); return; }
        std::string mode = req.get_param_value("mode");
        
        if(mode == "first") {
            scanResults.clear();
            int target = std::stoi(req.get_param_value("val"));
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            while(VirtualQueryEx(currentProcessHandle, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                if(mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE) {
                    std::vector<unsigned char> buffer(mbi.RegionSize);
                    SIZE_T read;
                    if(ReadProcessMemory(currentProcessHandle, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &read)) {
                        for(size_t i=0; i < read - 4; i += 4) {
                            if(*(int*)&buffer[i] == target) scanResults.push_back((uintptr_t)mbi.BaseAddress + i);
                        }
                    }
                }
                addr += mbi.RegionSize;
                if(addr > 0x7FFFFFFF || scanResults.size() > 500) break;
            }
        } 
        else if (mode == "next") {
            int target = std::stoi(req.get_param_value("val"));
            std::vector<uintptr_t> nextResults;
            for(auto& addr : scanResults) {
                int val = 0;
                ReadProcessMemory(currentProcessHandle, (LPCVOID)addr, &val, 4, NULL);
                if(val == target) nextResults.push_back(addr);
            }
            scanResults = nextResults;
        }

        std::string j = "[";
        for(size_t i=0; i < (scanResults.size() > 100 ? 100 : scanResults.size()); i++) {
            int v = 0;
            ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i], &v, 4, NULL);
            if(i > 0) j += ",";
            j += "{\"address\":\"" + ToHex(scanResults[i]) + "\",\"value\":" + std::to_string(v) + "}";
        }
        res.set_content(j + "]", "application/json");
    });

    // API: Değer Değiştirme
    svr.Get("/api/write", [](const httplib::Request& req, httplib::Response& res) {
        uintptr_t addr = std::stoull(req.get_param_value("addr"), nullptr, 16);
        int val = std::stoi(req.get_param_value("val"));
        if(WriteProcessMemory(currentProcessHandle, (LPVOID)addr, &val, sizeof(val), NULL))
            res.set_content("Deger Guncellendi!", "text/plain");
        else
            res.set_content("Yazma Hatasi!", "text/plain");
    });

    // API: Listeden Satır Silme
    svr.Get("/api/remove", [](const httplib::Request& req, httplib::Response& res) {
        int idx = std::stoi(req.get_param_value("idx"));
        if(idx >= 0 && idx < scanResults.size()) scanResults.erase(scanResults.begin() + idx);
        
        std::string j = "[";
        for(size_t i=0; i < (scanResults.size() > 100 ? 100 : scanResults.size()); i++) {
            int v = 0;
            ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i], &v, 4, NULL);
            if(i > 0) j += ",";
            j += "{\"address\":\"" + ToHex(scanResults[i]) + "\",\"value\":" + std::to_string(v) + "}";
        }
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(get_ui(), "text/html");
    });

    svr.listen("127.0.0.1", 1337);
    return 0;
}
