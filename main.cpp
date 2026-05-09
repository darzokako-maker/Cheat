#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include "httplib.h"

// Syscall için gerekli tanımlamalar
#define STATUS_SUCCESS 0x00000000

// Düşük Seviyeli Syscall Fonksiyonları (Assembly Simülasyonu)
extern "C" {
    // NtReadVirtualMemory Syscall (Windows 10/11 - 0x3F)
    NTSTATUS SyscallRead(HANDLE hProc, PVOID base, PVOID buf, SIZE_T size, PSIZE_T read) {
        typedef NTSTATUS(NTAPI* fNtRead)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
        static fNtRead fn = (fNtRead)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtReadVirtualMemory");
        return fn(hProc, base, buf, size, read);
    }

    // NtWriteVirtualMemory Syscall (Windows 10/11 - 0x3A)
    NTSTATUS SyscallWrite(HANDLE hProc, PVOID base, PVOID buf, SIZE_T size, PSIZE_T written) {
        typedef NTSTATUS(NTAPI* fNtWrite)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
        static fNtWrite fn = (fNtWrite)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtWriteVirtualMemory");
        return fn(hProc, base, buf, size, written);
    }
}

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

// ARAYÜZ (V9 Arayüzü Üzerine Geliştirilmiş)
std::string get_ui() {
    std::string html = "<html><head><meta charset='UTF-8'><title>Luna Pro V10 - Syscall</title>";
    html += "<style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;padding:20px;}";
    html += ".panel{background:#161b22;padding:20px;border-radius:10px;border:1px solid #30363d;margin-bottom:15px;}";
    html += "input,select,button{background:#0d1117;border:1px solid #30363d;color:#f0f6fc;padding:10px;border-radius:5px;}";
    html += "button{background:#238636;cursor:pointer;border:none;font-weight:bold;margin:2px;min-width:85px;} button:hover{opacity:0.8;}";
    html += ".btn-purple{background:#8957e5;} .btn-blue{background:#1f6feb;} .btn-red{background:#da3633;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:10px;} th,td{padding:10px;border-bottom:1px solid #30363d;text-align:left;}";
    html += "</style></head><body>";
    
    html += "<div class='panel'><h2>LUNA PRO V10 (SYSCALL MODE)</h2>";
    html += "<input type='text' id='procSearch' placeholder='İşlem Ara...' oninput='updateProcList()'><br>";
    html += "<select id='procList' style='width:100%;' size='6'></select><br>";
    html += "<button onclick='attach()' class='btn-blue' style='width:100%;margin-top:10px;'>İŞLEME BAĞLAN</button></div>";
    
    html += "<div class='panel'><h3>MANİPÜLASYON</h3>";
    html += "<input type='number' id='scanVal' placeholder='Değer'> ";
    html += "<button onclick=\"doScan('first')\">İLK TARAMA</button> ";
    html += "<button onclick=\"doScan('unknown')\" style='background:#484f58'>BİLİNMEYEN</button> <hr>";
    html += "<button class='btn-purple' onclick=\"doScan('increased')\">ARTTI</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('decreased')\">AZALDI</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('unchanged')\">DEĞİŞMEDİ</button>";
    html += "<div style='margin-top:10px; color:#58a6ff;'>Bulunan Adres: <span id='count'>0</span></div></div>";
    
    html += "<div class='panel'><table><thead><tr><th>ADRES</th><th>DEĞER</th><th>ÖNCEKİ</th><th>İŞLEM</th></tr></thead><tbody id='resTable'></tbody></table>";
    html += "<button onclick='massWrite()' class='btn-blue' style='margin-top:10px;width:100%;'>TOPLU DEĞİŞTİR</button></div>";

    html += "<script>";
    html += "var allProcs = []; function loadProcs(){ fetch('/api/procs').then(r=>r.json()).then(data=>{ allProcs=data; updateProcList(); }); }";
    html += "function updateProcList(){ var term=document.getElementById('procSearch').value.toLowerCase(); var s=document.getElementById('procList'); s.innerHTML=''; allProcs.filter(p=>p.name.toLowerCase().includes(term)).forEach(p=>{ var o=document.createElement('option'); o.value=p.id; o.text=p.name+' ['+p.id+']'; s.appendChild(o); }); }";
    html += "function attach(){ fetch('/api/attach?pid='+document.getElementById('procList').value).then(r=>r.text()).then(t=>alert(t)); }";
    html += "function doScan(mode){ var v=document.getElementById('scanVal').value; fetch('/api/scan?mode='+mode+'&val='+v).then(r=>r.json()).then(data=>{ renderTable(data.results); document.getElementById('count').innerText=data.total; }); }";
    html += "function renderTable(list){ var b=document.getElementById('resTable'); b.innerHTML=''; list.forEach(row=>{ b.innerHTML+='<tr><td>'+row.address+'</td><td><b>'+row.value+'</b></td><td>'+row.prev+'</td><td><button class=\"btn-blue\" onclick=\"writeVal(\\''+row.address+'\\')\">YAZ</button></td></tr>'; }); }";
    html += "function writeVal(addr){ var v=prompt('Yeni Değer:'); if(v) fetch('/api/write?addr='+addr+'&val='+v).then(()=>doScan('refresh')); }";
    html += "function massWrite(){ var v=prompt('Değer:'); if(v) fetch('/api/masswrite?val='+v).then(()=>doScan('refresh')); }";
    html += "window.onload=loadProcs;";
    html += "</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    // İşlem Listesi ve Bağlantı (V9 ile aynı, stabil)
    svr.Get("/api/procs", [](const httplib::Request&, httplib::Response& res) {
        std::string j = "[";
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if(h != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
            if(Process32First(h, &pe)) {
                bool first = true;
                do { if(!first) j += ","; j += "{\"id\":" + std::to_string(pe.th32ProcessID) + ",\"name\":\"" + std::string(pe.szExeFile) + "\"}"; first = false; } while(Process32Next(h, &pe));
            }
            CloseHandle(h);
        }
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/api/attach", [](const httplib::Request& req, httplib::Response& res) {
        currentPID = std::stoul(req.get_param_value("pid"));
        if(currentProcessHandle) CloseHandle(currentProcessHandle);
        currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
        res.set_content(currentProcessHandle ? "Syscall Baglantisi Aktif!" : "Erisim Hatasi!", "text/plain");
    });

    // SYSCALL TABANLI TARAMA MOTORU
    svr.Get("/api/scan", [](const httplib::Request& req, httplib::Response& res) {
        if(!currentProcessHandle) return;
        std::string mode = req.get_param_value("mode");
        
        if(mode == "first" || mode == "unknown") {
            scanResults.clear();
            int target = (mode == "first") ? std::stoi(req.get_param_value("val")) : 0;
            MEMORY_BASIC_INFORMATION mbi;
            uintptr_t addr = 0;
            while(VirtualQueryEx(currentProcessHandle, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                if(mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE) {
                    std::vector<int> buffer(mbi.RegionSize / 4);
                    SIZE_T read;
                    if(SyscallRead(currentProcessHandle, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &read) == STATUS_SUCCESS) {
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
            std::vector<ScanNode> next;
            for(auto& n : scanResults) {
                int cur = 0; SIZE_T r;
                if(SyscallRead(currentProcessHandle, (PVOID)n.address, &cur, 4, &r) == STATUS_SUCCESS) {
                    bool m = false;
                    if(mode == "increased") m = (cur > n.lastValue);
                    else if(mode == "decreased") m = (cur < n.lastValue);
                    else if(mode == "unchanged") m = (cur == n.lastValue);
                    else if(mode == "refresh") m = true;
                    if(m) { n.lastValue = cur; next.push_back(n); }
                }
            }
            scanResults = next;
        }

        std::string json = "{\"total\":" + std::to_string(scanResults.size()) + ",\"results\":[";
        for(size_t i=0; i < (scanResults.size() > 40 ? 40 : scanResults.size()); i++) {
            int v = 0; SIZE_T r;
            SyscallRead(currentProcessHandle, (PVOID)scanResults[i].address, &v, 4, &r);
            if(i > 0) json += ",";
            json += "{\"address\":\"" + ToHex(scanResults[i].address) + "\",\"value\":" + std::to_string(v) + ",\"prev\":" + std::to_string(scanResults[i].lastValue) + "}";
        }
        res.set_content(json + "]}", "application/json");
    });

    // SYSCALL TABANLI YAZMA
    svr.Get("/api/write", [](const httplib::Request& req, httplib::Response& res) {
        uintptr_t a = std::stoull(req.get_param_value("addr"), nullptr, 16);
        int v = std::stoi(req.get_param_value("val"));
        SyscallWrite(currentProcessHandle, (PVOID)a, &v, sizeof(v), NULL);
        res.set_content("Ok", "text/plain");
    });

    svr.Get("/api/masswrite", [](const httplib::Request& req, httplib::Response& res) {
        int v = std::stoi(req.get_param_value("val"));
        for(auto& n : scanResults) SyscallWrite(currentProcessHandle, (PVOID)n.address, &v, sizeof(v), NULL);
        res.set_content("Ok", "text/plain");
    });

    svr.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_content(get_ui(), "text/html"); });
    svr.listen("0.0.0.0", 1337);
    return 0;
}
