#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <sstream>

// OpenSSL gereksinimini kaldırdık
#include "httplib.h"

std::vector<uintptr_t> scanResults;
HANDLE currentProcessHandle = NULL;
DWORD currentPID = 0;

std::string ToHex(uintptr_t val) {
    std::stringstream ss;
    ss << std::uppercase << std::hex << val;
    return ss.str();
}

// Arayüzü daha sade ve hata vermeyecek tırnak yapılarıyla kurduk
const char* html_ui = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Luna Pro</title>
    <style>
        body { background: #0b0f19; color: #f1f5f9; font-family: monospace; padding: 20px; }
        .panel { background: #161d2f; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        input, select, button { background: #0b0f19; border: 1px solid #334155; color: white; padding: 8px; margin: 5px; }
        button { background: #38bdf8; color: black; cursor: pointer; font-weight: bold; border: none; }
        table { width: 100%; border-collapse: collapse; margin-top: 10px; }
        th, td { border: 1px solid #334155; padding: 8px; text-align: left; }
    </style>
</head>
<body>
    <div class="panel">
        <h2>LUNA STEALTH PRO</h2>
        <select id="procList"></select>
        <button onclick="attach()">BAGLAN</button>
        <hr>
        <input type="number" id="scanVal" placeholder="Deger">
        <button onclick="doScan('first')">ILK TARAMA</button>
        <button onclick="doScan('next')">FILTRELE</button>
    </div>
    <div class="panel">
        <table id="resTable">
            <thead><tr><th>ADRES</th><th>DEGER</th></tr></thead>
            <tbody></tbody>
        </table>
    </div>
    <script>
        function attach() {
            let pid = document.getElementById('procList').value;
            fetch('/api/attach?pid=' + pid).then(r => r.text()).then(t => alert(t));
        }
        function doScan(mode) {
            let val = document.getElementById('scanVal').value;
            fetch('/api/scan?mode=' + mode + '&val=' + val).then(r => r.json()).then(data => {
                let tbody = document.querySelector('#resTable tbody');
                tbody.innerHTML = '';
                data.forEach(row => {
                    tbody.innerHTML += '<tr><td>' + row.address + '</td><td>' + row.value + '</td></tr>';
                });
            });
        }
        fetch('/api/procs').then(r => r.json()).then(data => {
            let s = document.getElementById('procList');
            data.forEach(p => { s.innerHTML += '<option value="' + p.id + '">' + p.name + '</option>'; });
        });
    </script>
</body>
</html>
)";

int APIENTRY WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    // Konsolu tamamen gizle
    HWND hConsole = GetConsoleWindow();
    if (hConsole) ShowWindow(hConsole, SW_HIDE);

    httplib::Server svr;

    svr.Get("/", [](const auto&, auto& res) {
        res.set_content(html_ui, "text/html");
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
            currentPID = std::stoi(req.get_param_value("pid"));
            if(currentProcessHandle) CloseHandle(currentProcessHandle);
            currentProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, currentPID);
            res.set_content(currentProcessHandle ? "Baglandi!" : "Hata!", "text/plain");
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
            int v; ReadProcessMemory(currentProcessHandle, (LPCVOID)scanResults[i], &v, 4, NULL);
            if(i > 0) j += ",";
            j += "{\"address\":\"" + ToHex(scanResults[i]) + "\",\"value\":" + std::to_string(v) + "}";
        }
        res.set_content(j + "]", "application/json");
    });

    // Panic Key: CTRL+F12
    RegisterHotKey(NULL, 1, MOD_CONTROL, VK_F12);

    // Sunucuyu 1337 portunda baslat
    svr.listen("127.0.0.1", 1337);

    return 0;
}
