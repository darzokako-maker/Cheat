#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h> // NTSTATUS ve temel tanımlar için gerekli
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>
#include "httplib.h"

// Kütüphane bağlantıları
#pragma comment(lib, "ntdll.lib")

// Eksik olabilecek tanımları manuel ekleyelim (Hata almamak için)
typedef NTSTATUS(NTAPI* pNtReadVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
typedef NTSTATUS(NTAPI* pNtWriteVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

// Global Değişkenler
struct ScanNode {
    uintptr_t address;
    int lastValue;
};

std::vector<ScanNode> scanResults;
HANDLE currentProcessHandle = NULL;

// SYSCALL FONKSİYONLARI (Hatasız Çağrı Modu)
NTSTATUS SyscallRead(HANDLE hProc, PVOID base, PVOID buf, SIZE_T size, PSIZE_T read) {
    static pNtReadVM fn = (pNtReadVM)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtReadVirtualMemory");
    if (!fn) return (NTSTATUS)0xC0000001;
    return fn(hProc, base, buf, size, read);
}

NTSTATUS SyscallWrite(HANDLE hProc, PVOID base, PVOID buf, SIZE_T size, PSIZE_T written) {
    static pNtWriteVM fn = (pNtWriteVM)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtWriteVirtualMemory");
    if (!fn) return (NTSTATUS)0xC0000001;
    return fn(hProc, base, buf, size, written);
}

std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << val;
    return ss.str();
}

// HTML Arayüzü (Daha Stabil JavaScript)
std::string get_ui() {
    std::string html = "<html><head><meta charset='UTF-8'><title>Luna Pro V11</title>";
    html += "<style>body{background:#0d1117;color:#c9d1d9;font-family:sans-serif;padding:20px;}";
    html += ".panel{background:#161b22;padding:20px;border-radius:10px;border:1px solid #30363d;margin-bottom:15px;}";
    html += "input,select,button{background:#0d1117;border:1px solid #30363d;color:#f0f6fc;padding:10px;border-radius:5px;}";
    html += "button{background:#238636;cursor:pointer;border:none;font-weight:bold;margin:2px;min-width:85px;}";
    html += ".btn-purple{background:#8957e5;} .btn-blue{background:#1f6feb;} .btn-red{background:#da3633;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:10px;} th,td{padding:10px;border-bottom:1px solid #30363d;}";
    html += "</style></head><body>";
    
    html += "<div class='panel'><h2>LUNA PRO V11 (FIXED)</h2>";
    html += "<input type='text' id='pSearch' placeholder='Ara...' oninput='updateProcs()'><br>";
    html += "<select id='pList' style='width:100%; margin-top:5px;' size='5'></select><br>";
    html += "<button onclick='attach()' class='btn-blue' style='width:100%;margin-top:10px;'>BAĞLAN</button></div>";
    
    html += "<div class='panel'><h3>MANİPÜLASYON</h3>";
    html += "<input type='number' id='sVal' placeholder='Değer'> ";
    html += "<button onclick=\"doScan('first')\">İLK TARAMA</button> ";
    html += "<button onclick=\"doScan('unknown')\">BİLİNMEYEN</button><hr>";
    html += "<button class='btn-purple' onclick=\"doScan('increased')\">ARTTI</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('decreased')\">AZALDI</button> ";
    html += "<button class='btn-purple' onclick=\"doScan('unchanged')\">DEĞİŞMEDİ</button>";
    html += "<div style='margin-top:10px;'>Sonuç: <span id='count'>0</span></div></div>";
    
    html += "<div class='panel'><table><thead><tr><th>ADRES</th><th>DEĞER</th><th>ÖNCEKİ</th><th>AKSİYON</th></tr></thead><tbody id='resT'></tbody></table>";
    html += "<button onclick='massWrite()' class='btn-blue' style='width:100%; margin-top:10px;'>TOPLU DEĞİŞTİR</button></div>";

    html += "<script>";
    html += "var procs = []; function load(){ fetch('/api/procs').then(r=>r.json()).then(d=>{ procs=d; updateProcs(); }); }";
    html += "function updateProcs(){ var t=document.getElementById('pSearch').value.toLowerCase(); var s=document.getElementById('pList'); s.innerHTML=''; procs.filter(p=>p.name.toLowerCase().includes(t)).forEach(p=>{ var o=document.createElement('option'); o.value=p.id; o.text=p.name+' ['+p.id+']'; s.appendChild(o); }); }";
    html += "function attach(){ fetch('/api/attach?pid='+document.getElementById('pList').value).then(r=>r.text()).then(t=>alert(t)); }";
    html += "function doScan(m){ var v=document.getElementById('sVal').value; fetch('/api/scan?mode='+m+'&val='+v).then(r=>r.json()).then(d=>{ render(d.results); document.getElementById('count').innerText=d.total; }); }";
    html += "function render(l){ var b=document.getElementById('resT'); b.innerHTML=''; l.forEach(r=>{ b.innerHTML+='<tr><td>'+r.address+'</td><td><b>'+r.value+'</b></td><td>'+r.prev+'</td><td><button class=\"btn-blue\" onclick=\"writeV(\\''+r.address+'\\')\">YAZ</button></td></tr>'; }); }";
    html += "function writeV(a){ var v=prompt('Yeni Değer:'); if(v) fetch('/api/write?addr='+a+'&val='+v).then(()=>doScan('refresh')); }";
    html += "function massWrite(){ var v=prompt('Tüm Liste Değeri:'); if(v) fetch('/api/masswrite?val='+v).then(()=>doScan('refresh')); }";
    html += "window.onload=load;";
    html += "</script></body></html>";
    return html;
}

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    httplib::Server svr;

    svr.Get("/api/procs", [](const httplib::Request&, httplib::Response& res) {
        std::string j = "[";
        HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if(h != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
            if(Process32First(h, &pe)) {
                bool f = true;
                do { if(!f) j += ","; j += "{\"id\":" + std::to_string(pe.th32ProcessID) + ",\"name\":\"" + std::string(pe.szExeFile) + "\"}"; f = false; } while(Process32Next(h, &pe));
            }
            CloseHandle(h);
        }
        res.set_content(j + "]", "application/json");
    });

    svr.Get("/api/attach", [](const httplib::Request& req, httplib::Response& res) {
        DWORD pid = std::stoul(req.get_param_value("pid"));
        if(currentProcessHandle) CloseHandle(currentProcessHandle);
        currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        res.set_content(currentProcessHandle ? "Baglandi!" : "Hata!", "text/plain");
    });

    svr.Get("/api/scan", [](const httplib::Request& req, httplib::Response& res) {
        if(!currentProcessHandle) return;
        std::string mode = req.get_param_value("mode");
        if(mode == "first" || mode == "unknown") {
            scanResults.clear();
            int target = (mode == "first") ? std::stoi(req.get_param_value("val")) : 0;
            MEMORY_BASIC_INFORMATION mbi; uintptr_t addr = 0;
            while(VirtualQueryEx(currentProcessHandle, (LPCVOID)addr, &mbi, sizeof(mbi))) {
                if(mbi.State == MEM_COMMIT && mbi.Protect == PAGE_READWRITE) {
                    std::vector<int> buf(mbi.RegionSize / 4); SIZE_T r;
                    if(SyscallRead(currentProcessHandle, mbi.BaseAddress, buf.data(), mbi.RegionSize, &r) == 0) {
                        for(size_t i=0; i<buf.size(); i++) {
                            if(mode == "unknown" || buf[i] == target)
                                scanResults.push_back({ (uintptr_t)mbi.BaseAddress + (i*4), buf[i] });
                        }
                    }
                }
                addr += mbi.RegionSize;
                if(addr > 0x7FFFFFFF || scanResults.size() > 30000) break;
            }
        } else {
            std::vector<ScanNode> next;
            for(auto& n : scanResults) {
                int cur = 0; SIZE_T r;
                if(SyscallRead(currentProcessHandle, (PVOID)n.address, &cur, 4, &r) == 0) {
                    bool ok = false;
                    if(mode == "increased") ok = (cur > n.lastValue);
                    else if(mode == "decreased") ok = (cur < n.lastValue);
                    else if(mode == "unchanged") ok = (cur == n.lastValue);
                    else if(mode == "refresh") ok = true;
                    if(ok) { n.lastValue = cur; next.push_back(n); }
                }
            }
            scanResults = next;
        }

        std::string json = "{\"total\":" + std::to_string(scanResults.size()) + ",\"results\":[";
        for(size_t i=0; i < (scanResults.size() > 30 ? 30 : scanResults.size()); i++) {
            int v = 0; SIZE_T r; SyscallRead(currentProcessHandle, (PVOID)scanResults[i].address, &v, 4, &r);
            if(i > 0) json += ",";
            json += "{\"address\":\"" + ToHex(scanResults[i].address) + "\",\"value\":" + std::to_string(v) + ",\"prev\":" + std::to_string(scanResults[i].lastValue) + "}";
        }
        res.set_content(json + "]}", "application/json");
    });

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
