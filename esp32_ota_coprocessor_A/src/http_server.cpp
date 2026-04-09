#include "http_server.h"
#include "fw_manager.h"
#include "config.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);

// ---------------------------------------------------------------------------
// Embedded HTML UI — served at GET /
// ---------------------------------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>OTA Firmware Uploader</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:sans-serif;background:#f5f5f5;display:flex;justify-content:center;padding:32px 16px}
    .card{background:#fff;border-radius:12px;padding:24px;max-width:460px;width:100%;box-shadow:0 2px 12px #0002}
    h2{color:#222;margin-bottom:20px;font-size:1.3rem}
    .status{padding:12px 16px;border-radius:8px;margin-bottom:12px;font-size:.95rem}
    .ready   {background:#d4edda;color:#155724}
    .empty   {background:#fff3cd;color:#7d5a00}
    .busy    {background:#cce5ff;color:#004085}
    .error   {background:#f8d7da;color:#721c24}
    .conn    {background:#d4edda;color:#155724}
    .disconn {background:#e2e3e5;color:#383d41}
    label{display:block;margin-bottom:6px;font-weight:600;color:#444}
    input[type=file]{width:100%;padding:8px;border:2px dashed #ccc;border-radius:6px;
                     background:#fafafa;cursor:pointer;margin-bottom:14px}
    button{width:100%;padding:13px;background:#0066cc;color:#fff;border:none;
           border-radius:8px;font-size:1rem;cursor:pointer;font-weight:600;transition:.2s}
    button:disabled{background:#9bb8d4;cursor:default}
    #msg{margin-top:12px;font-size:.9rem;color:#555;min-height:1.2em;text-align:center}
    progress{width:100%;height:8px;border-radius:4px;margin-top:10px;display:none}
  </style>
</head>
<body>
<div class="card">
  <h2>⚡ OTA Co-processor A</h2>
  <div id="conn" class="status disconn">&#8226; C5-B desconectado</div>
  <div id="st" class="status empty">Cargando estado...</div>
  <label>Firmware Nordic DFU (.zip):</label>
  <input type="file" id="file" accept=".zip">
  <button id="btn" onclick="upload()">Subir y extraer</button>
  <progress id="prog" max="100"></progress>
  <div id="msg"></div>
</div>
<script>
  function setStatus(s){
    const el=document.getElementById('st');
    el.className='status '+s.css;
    el.innerHTML=s.html;
  }
  function refreshStatus(){
    fetch('/status').then(r=>r.json()).then(d=>{
      // Firmware state
      if(d.state==='ready')
        setStatus({css:'ready',html:'&#10003; Firmware listo &nbsp;|&nbsp; DAT: '+d.dat_b+' B &nbsp; BIN: '+(d.bin_b/1024).toFixed(1)+' KB'});
      else if(d.state==='extracting')
        setStatus({css:'busy',html:'&#8987; Extrayendo zip...'});
      else if(d.state==='uploading')
        setStatus({css:'busy',html:'&#8987; Subiendo archivo...'});
      else if(d.state==='error')
        setStatus({css:'error',html:'&#10007; Error en la extracción'});
      else
        setStatus({css:'empty',html:'&#9888; Sin firmware cargado'});
      // C5-B connection state
      const conn=document.getElementById('conn');
      if(d.clients>0){
        conn.className='status conn';
        conn.innerHTML='&#9679; C5-B conectado';
      } else {
        conn.className='status disconn';
        conn.innerHTML='&#8226; C5-B desconectado';
      }
    }).catch(()=>setStatus({css:'empty',html:'&#9888; Sin firmware cargado'}));
  }
  refreshStatus();
  setInterval(refreshStatus, 3000);

  function upload(){
    const f=document.getElementById('file').files[0];
    if(!f){alert('Selecciona un archivo .zip');return;}
    const btn=document.getElementById('btn');
    const prog=document.getElementById('prog');
    const msg=document.getElementById('msg');
    btn.disabled=true;
    prog.style.display='block';
    prog.value=0;
    msg.textContent='Subiendo...';
    const fd=new FormData();
    fd.append('file',f);
    const xhr=new XMLHttpRequest();
    xhr.open('POST','/upload');
    xhr.upload.onprogress=e=>{
      if(e.lengthComputable){
        prog.value=Math.round(e.loaded/e.total*100);
        msg.textContent='Subiendo... '+prog.value+'%';
      }
    };
    xhr.onload=()=>{
      prog.style.display='none';
      btn.disabled=false;
      try{
        const r=JSON.parse(xhr.responseText);
        msg.textContent=r.ok?'Upload completo, extrayendo...':'Error: '+(r.error||'desconocido');
      }catch{msg.textContent='Respuesta inesperada del servidor';}
      refreshStatus();
    };
    xhr.onerror=()=>{
      prog.style.display='none';
      btn.disabled=false;
      msg.textContent='Error de red durante la subida';
    };
    xhr.send(fd);
  }
</script>
</body>
</html>)html";

// ---------------------------------------------------------------------------
// Upload state
// ---------------------------------------------------------------------------
static File s_upload_file;

static void handleUpload(AsyncWebServerRequest *req,
                         const String &filename, size_t index,
                         uint8_t *data, size_t len, bool final)
{
    if (index == 0) {
        Serial.printf("[HTTP] Upload start: %s\n", filename.c_str());
        LittleFS.remove(ZIP_TMP_PATH);
        s_upload_file = LittleFS.open(ZIP_TMP_PATH, "w");
        g_fw_state    = FwState::UPLOADING;
        if (!s_upload_file) {
            Serial.println("[HTTP] Cannot open temp file for writing");
            return;
        }
    }

    if (s_upload_file) {
        s_upload_file.write(data, len);
    }

    if (final) {
        if (s_upload_file) {
            Serial.printf("[HTTP] Upload done: %u bytes\n", (unsigned)(index + len));
            s_upload_file.close();
            fwManagerUploadDone(); // queue extraction for loop()
        }
    }
}

// ---------------------------------------------------------------------------
// httpServerBegin
// ---------------------------------------------------------------------------
void httpServerBegin()
{
    // Web UI
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    // Status JSON
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        const char *state_str = "empty";
        switch (g_fw_state) {
            case FwState::READY:      state_str = "ready";      break;
            case FwState::UPLOADING:  state_str = "uploading";  break;
            case FwState::EXTRACTING: state_str = "extracting"; break;
            case FwState::ERROR:      state_str = "error";      break;
            default: break;
        }
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"state\":\"%s\",\"dat_b\":%u,\"bin_b\":%u,\"clients\":%u}",
                 state_str,
                 (unsigned)fwDatSize(),
                 (unsigned)fwBinSize(),
                 (unsigned)WiFi.softAPgetStationNum());
        req->send(200, "application/json", buf);
    });

    // File upload endpoint
    server.on("/upload", HTTP_POST,
        [](AsyncWebServerRequest *req) {
            // Response sent after upload handler finishes
            bool ok = (g_fw_state == FwState::EXTRACTING || g_fw_state == FwState::READY);
            req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"write failed\"}");
        },
        handleUpload
    );

    // Serve firmware files to co-processor B
    server.serveStatic("/firmware.dat", LittleFS, FW_DAT_PATH);
    server.serveStatic("/firmware.bin", LittleFS, FW_BIN_PATH);

    // 404
    server.onNotFound([](AsyncWebServerRequest *req) {
        req->send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("[HTTP] Server started on port 80");
}
