#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include "httplib.h"

std::vector<uintptr_t> scanResults;
HANDLE currentProcessHandle = NULL;
DWORD currentPID = 0;

std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << val;
    return ss.str();
}

// ARAYÜZ: İşlem arama ve tablo geliştirmeleri
std::string get_ui() {
    std::string html = "<html><head><meta charset=\"UTF-8\"><title>Luna Pro V5</title>";
    html += "<style>body{background:#0d1117;color:#c9d1d9;font-family:'Segoe UI',sans-serif;padding:20px;}";
    html += ".panel{background:#161b22;padding:20px;border-radius:10px;border:1px solid #30363d;margin-bottom:15px;}";
    html += "input,select,button{background:#0d1117;border:1px solid #30363d;color:#f0f6fc;padding:10px;border-radius:5px;}";
    html += "button{background:#238636;cursor:pointer;border:none;font-weight:bold;transition:0.2s;} button:hover{opacity:0.8;}";
    html += ".btn-edit{background:#1f6feb;} .btn-del{background:#da3633;}";
    html += "table{width:100%;border-collapse:collapse;} th,td{padding:12px;border-bottom:1px solid #30363d;text-align:left;}";
    html += "#procSearch{width:200px;margin-bottom:10px;border-color:#1f6feb;}";
    html += "tr:hover{background:#1c2128;}</style></head><body>";
    
    html += "<div class=\"panel\"><h2>LUNA PRO V5</h2>";
    html += "<input type=\"text\" id=\"procSearch\" placeholder=\"İşlem Ara...\" onkeyup=\"filterProcs()\"> ";
    html += "<select id=\"procList\" style=\"width:300px;\"></select> ";
    html += "<button onclick=\"attach()\">BAĞLAN</button></div>";
    
    html += "<div class=\"panel\"><h3>BELLEK MOTORU</h3>";
    html += "<input type=\"number\" id=\"scanVal\" placeholder=\"Sayısal Değer\"> ";
    html += "<button onclick=\"doScan('first')\">İLK TARAMA</button> ";
    html += "<button onclick=\"doScan('next')\" style=\"background:#8957e5\">FİLTRELE</button></div>";
    
    html += "<div class=\"panel\"><table><thead><tr><th>ADRES</th><th>DEĞER</th><th>YÖNET</th></tr></thead>";
    html += "<tbody id=\"resTable\"></tbody></table></div>";

    html += "<script>";
    // İşlem Listesini Filtreleme
    html += "function filterProcs(){ var input = document.getElementById('procSearch').value.toLowerCase();";
    html += "var select = document.getElementById('procList'); for(var i=0; i<select.options.length; i++){";
    html += "var txt = select.options[i].text.toLowerCase(); select.options[i].style.display = txt.includes(input) ? '' : 'none'; } }";

    html += "function attach(){ var pid = document.getElementById('procList').value;";
    html += "fetch('/api/attach?pid='+pid).then(r=>r.text()).then(t=>alert(t));}";
    
    html += "function doScan(mode){ var val = document.getElementById('scanVal').value;";
    html += "document.getElementById('resTable').innerHTML = '<tr><td colspan=\"3\">Bellek taranıyor, bu işlem RAM boyutuna göre sürebilir...</td></tr>';";
    html += "fetch('/api/scan?mode='+mode+'&val='+val).then(r=>r.json()).then(data=>{ renderTable(data); }); }";
    
    html += "function renderTable(data){ var b = document.getElementById('resTable'); b.innerHTML = '';";
    html += "data.forEach((row, index)=>{ b.innerHTML += '<tr><td>'+row.address+'</td><td><b>'+row.value+'</b></td>' +";
    html += "'<td><button class=\"btn-edit\" onclick=\"writeVal(\\''+row.address+'\\')\">YAZ</button>' +";
    html += "'<button class=\"btn-del\" onclick=\"removeRow('+index+')\">AT</button></td></tr>'; }); }";
    
    html += "function writeVal(addr){ var v = prompt('Yeni Değer:'); if(v) fetch('/api/write?addr='+addr+'&val='+v).then(r=>r.text()).then(t=>doScan('refresh')); }";
    html += "function removeRow(idx){ fetch('/api/remove?idx='+idx).then(r=>r.json()).then(data=>renderTable(data)); }";
    
    html += "fetch('/api/procs').then(r=>r.json()).then(data=>{ var s = document.getElementById('procList');";
    html += "data.forEach(p=>{ var o = document.createElement('option'); o.value=p.id; o.text=p.name + ' (' + p.id + ')'; s.appendChild(o); }); });";
    html += "</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

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

    svr.Get("/api/attach", [](const httplib::Request& req, httplib::Response& res) {
        currentPID = std::stoul(req.get_param_value("pid"));
        if(currentProcessHandle) CloseHandle(currentProcessHandle);
        currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
        res.set_content(currentProcessHandle ? "Sürece Bağlanıldı!" : "Hata: Erişim Reddedildi!", "text/plain");
    });

    svr.Get("/api/scan", [](const httplib::Request& req, httplib::Response& res) {
        if(!currentProcessHandle) { res.set_content("[]", "application/json"); return; }
        std::string mode = req.get_param_value("mode");
        
        if(mode == "first") {
            scanResults.clear();
            int target = std::stoi(req.get_param_value("val"));
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            while(VirtualQueryEx(currentProcessHandle, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                // Sadece yazılabilir ve fiziksel bellekteki sayfaları tara
                if(mbi.State == MEM_COMMIT && (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
                    std::vector<int> buffer(mbi.RegionSize / 4);
                    SIZE_T bytesRead;
                    if(ReadProcessMemory(currentProcessHandle, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead)) {
                        for(size_t i=0; i < bytesRead / 4; i++) {
                            if(buffer[i] == target) scanResults.push_back((uintptr_t)mbi.BaseAddress + (i * 4));
                        }
                    }
                }
                addr += mbi.RegionSize;
                if(addr > 0x7FFFFFFF || scanResults.size() > 1000) break;
            }
        } else if(mode == "next") {
            int target = std::stoi(req.get_param_value("val"));
            std::vector<uintptr_t> filtered;
            for(uintptr_t a : scanResults) {
                int currentVal = 0;
                if(ReadProcessMemory(currentProcessHandle, (LPCVOID)a, &currentVal, 4, NULL)) {
                    if(currentVal == target) filtered.push_back(a);
                }
            }
            scanResults = filtered;
        }

        std::string j = "[";
        for(size_t i=0; i < (scanResults.size() > 50 ? 50 : scanResults.size()); i++) {
            int v = 0; ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i], &v, 4, NULL);
            if(i > 0) j += ",";
            j += "{\"address\":\"" + ToHex(scanResults[i]) + "\",\"value\":" + std::to_string(v) + "}";
        }
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/api/write", [](const httplib::Request& req, httplib::Response& res) {
        uintptr_t addr = std::stoull(req.get_param_value("addr"), nullptr, 16);
        int val = std::stoi(req.get_param_value("val"));
        WriteProcessMemory(currentProcessHandle, (LPVOID)addr, &val, sizeof(val), NULL);
        res.set_content("Tamam", "text/plain");
    });

    svr.Get("/api/remove", [](const httplib::Request& req, httplib::Response& res) {
        int idx = std::stoi(req.get_param_value("idx"));
        if(idx >= 0 && idx < scanResults.size()) scanResults.erase(scanResults.begin() + idx);
        res.set_content("[]", "application/json"); // Tablo JS tarafında renderTable ile güncellenecek
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(get_ui(), "text/html");
    });

    svr.listen("127.0.0.1", 1337);
    return 0;
}
