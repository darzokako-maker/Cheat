#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>

#include "httplib.h"

std::vector<uintptr_t> scanResults;
HANDLE currentProcessHandle = NULL;
DWORD currentPID = 0;

std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << std::uppercase << std::hex << val;
    return ss.str();
}

// Web arayüzünü parçalara ayırarak en güvenli şekilde tanımlıyoruz
std::string get_ui() {
    std::string html = "<html><head><meta charset=\"UTF-8\"><title>Luna Pro</title>";
    html += "<style>body{background:#0b0f19;color:#f1f5f9;font-family:monospace;padding:20px;}";
    html += ".panel{background:#161d2f;padding:20px;border-radius:8px;margin-bottom:20px;}";
    html += "input,select,button{background:#0b0f19;border:1px solid #334155;color:white;padding:8px;margin:5px;}";
    html += "button{background:#38bdf8;color:black;cursor:pointer;font-weight:bold;border:none;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:10px;} th,td{border:1px solid #334155;padding:8px;}</style></head>";
    html += "<body><div class=\"panel\"><h2>LUNA STEALTH PRO</h2><select id=\"procList\"></select>";
    html += "<button onclick=\"attach()\">BAGLAN</button><hr>";
    html += "<input type=\"number\" id=\"scanVal\" placeholder=\"Deger\">";
    html += "<button onclick=\"doScan('first')\">ILK TARAMA</button></div>";
    html += "<div class=\"panel\"><table><thead><tr><th>ADRES</th><th>DEGER</th></tr></thead><tbody id=\"resTable\"></tbody></table></div>";
    html += "<script>function attach(){var p=document.getElementById('procList').value;fetch('/api/attach?pid='+p).then(r=>r.text()).then(t=>alert(t));}";
    html += "function doScan(m){var v=document.getElementById('scanVal').value;var p=document.getElementById('procList').value;";
    html += "fetch('/api/scan?mode='+m+'&val='+v).then(r=>r.json()).then(data=>{var b=document.getElementById('resTable');b.innerHTML='';";
    html += "data.forEach(row=>{b.innerHTML+='<tr><td>'+row.address+'</td><td>'+row.value+'</td></tr>';});});}";
    html += "fetch('/api/procs').then(r=>r.json()).then(data=>{var s=document.getElementById('procList');";
    html += "data.forEach(p=>{var o=document.createElement('option');o.value=p.id;o.text=p.name;s.appendChild(o);});});</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
    httplib::Server svr;

    svr.Get("/", [](const auto&, auto& res) {
        res.set_content(get_ui(), "text/html");
    });

    svr.Get("/api/procs", [](const auto&, auto& res) {
        std::string j = "[";
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
        if(Process32First(h, &pe)) {
            bool first = true;
            do {
                if(!first) j += ",";
                j += "{\"id\":" + std::to_string(pe.th32ProcessID) + ",\"name\":\"" + pe.szExeFile + "\"}";
                first = false;
            } while(Process32Next(h, &pe));
        }
        CloseHandle(h);
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/api/attach", [](const auto& req, auto& res) {
        if(req.has_param("pid")) {
            currentPID = std::get<1>(*req.params.find("pid"));
            if(currentProcessHandle) CloseHandle(currentProcessHandle);
            currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
            res.set_content(currentProcessHandle ? "Baglandi!" : "Erişim Reddedildi!", "text/plain");
        }
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
        }
        std::string j = "[";
        for(size_t i=0; i < (scanResults.size() > 50 ? 50 : scanResults.size()); i++) {
            int v = 0; ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i], &v, 4, NULL);
            if(i > 0) j += ",";
            j += "{\"address\":\"" + ToHex(scanResults[i]) + "\",\"value\":" + std::to_string(v) + "}";
        }
        res.set_content(j + "]", "application/json");
    });

    svr.listen("127.0.0.1", 1337);
    return 0;
}
