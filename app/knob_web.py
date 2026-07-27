#!/usr/bin/env python3
# SmartKnob kontrol paneli — tarayici tabanli (stdlib + pyserial, EK KURULUM YOK)
# Calistir:  python3 app/knob_web.py     -> tarayicida http://localhost:8787
#
# Knob seri protokolu (foc firmware):
#   MODE volume|spaces | d <n> | k <Kdet> | W <Kwall> | b <damping>
#   COL b|a <r> <g> <b> | SP <n> <idx> | H | L | ? | CFG?

import glob
import json
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
except ImportError:
    raise SystemExit("pyserial yok:  pip install pyserial")

BAUD = 115200
PORT = 8787
LOG = []            # tum gelen satirlar
LOG_LOCK = threading.Lock()


def list_ports():
    return sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*"))


class Knob:
    def __init__(self):
        self.ser = None
        self._stop = threading.Event()
        self._rx = ""

    def connect(self, port):
        self.disconnect()
        self.ser = serial.Serial(port, BAUD, timeout=0.1)
        self._stop.clear()
        threading.Thread(target=self._reader, daemon=True).start()
        self._push(f"[baglandi] {port}")

    def disconnect(self):
        self._stop.set()
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def is_open(self):
        return self.ser is not None and self.ser.is_open

    def send(self, line):
        if self.is_open():
            try:
                self.ser.write((line + "\n").encode())
                self.ser.flush()
                self._push("> " + line)
            except Exception as e:
                self._push(f"[write hata] {e}")

    def _reader(self):
        while not self._stop.is_set() and self.ser:
            try:
                data = self.ser.read(256)
            except Exception:
                break
            if not data:
                continue
            self._rx += data.decode(errors="replace")
            while "\n" in self._rx:
                line, self._rx = self._rx.split("\n", 1)
                line = line.strip("\r")
                if line:
                    self._push(line)

    @staticmethod
    def _push(line):
        with LOG_LOCK:
            LOG.append(line)


KNOB = Knob()

HTML = r"""<!doctype html><html lang="tr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartKnob</title>
<style>
  :root{color-scheme:dark}
  *{box-sizing:border-box}
  body{margin:0;font:14px -apple-system,system-ui,sans-serif;background:#0e0f13;color:#e7e7ea}
  .wrap{max-width:460px;margin:0 auto;padding:16px}
  h1{font-size:18px;margin:0 0 12px}
  fieldset{border:1px solid #2a2c34;border-radius:10px;margin:0 0 12px;padding:12px}
  legend{color:#9aa0ad;padding:0 6px;font-size:12px;text-transform:uppercase;letter-spacing:.5px}
  select,button{background:#1a1c22;color:#e7e7ea;border:1px solid #333743;border-radius:8px;padding:8px 10px;font:inherit}
  button{cursor:pointer}button:hover{background:#23262e}
  button.on{background:#2563eb;border-color:#2563eb}
  .row{display:flex;align-items:center;gap:10px;margin:8px 0}
  .row label{flex:0 0 120px;color:#c3c7d1}
  input[type=range]{flex:1}
  input[type=color]{width:42px;height:30px;border:none;background:none;padding:0}
  .val{width:44px;text-align:right;color:#8ea0ff;font-variant-numeric:tabular-nums}
  .modes button{flex:1}
  #log{height:150px;overflow:auto;background:#000;border-radius:8px;padding:8px;
       font:12px Menlo,monospace;color:#5ee08a;white-space:pre-wrap}
  .muted{color:#6b7280;font-size:12px}
</style></head><body><div class="wrap">
<h1>🎛️ SmartKnob</h1>

<fieldset><legend>Baglanti</legend>
  <div class="row">
    <select id="port" style="flex:1"></select>
    <button onclick="refresh()">⟳</button>
    <button id="conn" onclick="toggleConn()">Baglan</button>
  </div>
</fieldset>

<fieldset><legend>Mod — knob ne yapiyor</legend>
  <div class="row modes">
    <button id="m_volume" onclick="setMode('volume')">🔊 Ses</button>
    <button id="m_spaces" onclick="setMode('spaces')">🖥️ Masaustu</button>
    <button id="m_tabs" onclick="setMode('tabs')">🌐 Chrome</button>
  </div>
</fieldset>

<fieldset><legend>Renkler (secili mod)</legend>
  <div class="row"><label>Taban / ring</label>
    <input type="color" id="col_b" onchange="sendCol('b',this.value)"></div>
  <div class="row"><label>Aktif / seviye</label>
    <input type="color" id="col_a" onchange="sendCol('a',this.value)"></div>
</fieldset>

<fieldset><legend>Haptik his</legend>
  <div class="row"><label>Centik sayisi</label>
    <input type="range" id="s_d" min="2" max="24" step="1" oninput="sv('d',this.value)" onchange="sendS('d')">
    <span class="val" id="v_d">17</span></div>
  <div class="row"><label>Centik sertligi</label>
    <input type="range" id="s_k" min="0" max="60" step="1" oninput="sv('k',this.value)" onchange="sendS('k')">
    <span class="val" id="v_k">22</span></div>
  <div class="row"><label>Duvar sertligi</label>
    <input type="range" id="s_W" min="0" max="200" step="5" oninput="sv('W',this.value)" onchange="sendS('W')">
    <span class="val" id="v_W">40</span></div>
  <div class="row"><label>Sonumleme</label>
    <input type="range" id="s_b" min="0" max="1" step="0.05" oninput="sv('b',this.value)" onchange="sendS('b')">
    <span class="val" id="v_b">0.30</span></div>
</fieldset>

<fieldset><legend>Buton esleme — fiziksel sira karisiksa</legend>
  <div class="muted" style="margin-bottom:8px">Her fonksiyon icin <b>Ogren</b>'e bas, sonra atamak istedigin fiziksel butona bas.</div>
  <div id="btnmap"></div>
</fieldset>

<fieldset><legend>Deploy sifresi (kasa kilidi)</legend>
  <div class="row">
    <input type="number" id="p0" min="1" max="19" value="3" style="width:60px">
    <input type="number" id="p1" min="1" max="19" value="5" style="width:60px">
    <input type="number" id="p2" min="1" max="19" value="7" style="width:60px">
    <input type="number" id="p3" min="1" max="19" value="1" style="width:60px">
    <button onclick="setPin()">Kaydet</button>
  </div>
  <div class="muted">Sirasiyla: sag / sol / sag / sol tik sayisi. Knob P2 -> sifre -> kol -> deploy.</div>
</fieldset>

<fieldset><legend>Aksiyon</legend>
  <div class="row">
    <button onclick="send('H')">Home</button>
    <button onclick="send('L')">LED test</button>
    <button onclick="send('?')">Durum</button>
    <button onclick="send('CFG?')">Ayarlari cek</button>
  </div>
  <div class="muted">Space sayisi + aktif index normalde Hammerspoon'dan otomatik gelir (SP n idx).</div>
</fieldset>

<fieldset><legend>Log</legend><div id="log"></div></fieldset>
</div>
<script>
const $=id=>document.getElementById(id);
let connected=false;
async function api(path,body){const r=await fetch(path,{method:body?'POST':'GET',
  headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):null});return r.json();}
async function refresh(){const d=await api('/ports');const s=$('port');s.innerHTML='';
  d.ports.forEach(p=>{const o=document.createElement('option');
    o.value=p;o.textContent=p.includes('usbmodem')?(p+'  ← native (bunu sec)'):(p+'  (flash UART)');
    s.appendChild(o);});
  // Firmware Serial NATIVE USB'de (usbmodem). usbserial = sadece flash -> okuma/yazma CALISMAZ.
  const nat=d.ports.find(p=>p.includes('usbmodem'));
  if(nat)s.value=nat; else if(d.ports.length)s.value=d.ports[d.ports.length-1];}
async function toggleConn(){if(connected){await api('/disconnect',{});connected=false;$('conn').textContent='Baglan';$('conn').classList.remove('on');}
  else{await api('/connect',{port:$('port').value});connected=true;$('conn').textContent='Kes';$('conn').classList.add('on');
    setTimeout(()=>{send('CFG?');send('BTNMAP?');},400);}}
function send(line){api('/send',{line});}
function setMode(m){send('MODE '+m);highlightMode(m);setTimeout(()=>send('CFG?'),200);}
function highlightMode(m){['volume','spaces','tabs'].forEach(x=>$('m_'+x).classList.toggle('on',x===m));}
function setPin(){const v=i=>parseInt($('p'+i).value)||1;
  send('PIN '+v(0)+' '+v(1)+' '+v(2)+' '+v(3));}
function sendCol(slot,hex){const r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);
  send('COL '+slot+' '+r+' '+g+' '+b);}
function sv(k,v){$('v_'+k).textContent=(k==='b')?parseFloat(v).toFixed(2):v;}
function sendS(k){const v=$('s_'+k).value;send((k==='b')?('b '+parseFloat(v).toFixed(2)):(k+' '+parseInt(v)));}
function hex(csv){return '#'+csv.split(',').map(x=>(+x).toString(16).padStart(2,'0')).join('');}
// ---- Buton esleme (act ID'ler firmware enum ile AYNI olmali) ----
const FUNCS=[{act:1,name:'🔊 Ses'},{act:2,name:'🖥️ Masaustu'},{act:3,name:'☀️ Parlaklik'},
  {act:4,name:'🚀 Deploy'},{act:5,name:'🤖 Claude'},{act:6,name:'🌐 Chrome'},
  {act:7,name:'📐 Fusion'},{act:8,name:'🖱️ Scroll'}];
let actToBit={};          // act -> hangi fiziksel bit
let learnAct=null;        // su an ogrenilen fonksiyon (act)
function renderBtnMap(){const box=$('btnmap');box.innerHTML='';
  FUNCS.forEach(f=>{const row=document.createElement('div');row.className='row';
    const b=actToBit[f.act];
    row.innerHTML=`<label style="flex:0 0 130px">${f.name}</label>
      <span class="val" style="width:70px" id="bit_${f.act}">${b===undefined?'—':('Buton '+(b+1))}</span>
      <button id="learn_${f.act}" onclick="learn(${f.act})">Ogren</button>`;
    box.appendChild(row);});
}
function learn(act){learnAct=act;
  FUNCS.forEach(f=>{const bt=$('learn_'+f.act);if(bt)bt.classList.remove('on');});
  const b=$('learn_'+act);if(b){b.classList.add('on');b.textContent='Butona bas...';}}
function applyBtnPress(bit){if(learnAct===null)return;
  send('BTN '+bit+' '+learnAct);
  // ayni bit baska fonksiyondaysa onu temizle (tek buton tek is)
  for(const a in actToBit){if(actToBit[a]===bit&&+a!==learnAct)delete actToBit[a];}
  actToBit[learnAct]=bit;const done=learnAct;learnAct=null;
  const b=$('learn_'+done);if(b){b.classList.remove('on');b.textContent='Ogren';}
  renderBtnMap();}
let seen=0;
async function poll(){try{const d=await api('/poll?since='+seen);
  if(d.lines.length){const L=$('log');d.lines.forEach(x=>{L.textContent+=x+'\n';parseLine(x);});L.scrollTop=L.scrollHeight;seen=d.next;}
}catch(e){}setTimeout(poll,300);}
function parseLine(line){
  if(line.startsWith('BTNPRESS')){const bit=parseInt(line.split(/\s+/)[1]);
    if(!isNaN(bit))applyBtnPress(bit);return;}
  if(line.startsWith('BTNMAP')){const a=line.split(/\s+/).slice(1).map(x=>parseInt(x));
    actToBit={};a.forEach((act,bit)=>{if(act>0)actToBit[act]=bit;});renderBtnMap();return;}
  if(!line.startsWith('CFG'))return;const kv={};
  line.split(/\s+/).slice(1).forEach(t=>{const i=t.indexOf('=');if(i>0)kv[t.slice(0,i)]=t.slice(i+1);});
  if(kv.mode!==undefined)highlightMode(kv.mode==='0'?'volume':kv.mode==='1'?'spaces':'tabs');
  const map={detents:'d',Kdet:'k',Kwall:'W',damping:'b'};
  for(const f in map){if(kv[f]!==undefined){const s=$('s_'+map[f]);s.value=kv[f];sv(map[f],kv[f]);}}
  if(kv.base)$('col_b').value=hex(kv.base);
  if(kv.act)$('col_a').value=hex(kv.act);}
renderBtnMap();refresh();poll();
</script></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _json(self, obj):
        b = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def _body(self):
        n = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(n) or b"{}") if n else {}

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            b = HTML.encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
        elif self.path == "/ports":
            self._json({"ports": list_ports()})
        elif self.path.startswith("/poll"):
            since = 0
            if "since=" in self.path:
                try:
                    since = int(self.path.split("since=")[1])
                except Exception:
                    since = 0
            with LOG_LOCK:
                lines = LOG[since:]
                nxt = len(LOG)
            self._json({"lines": lines, "next": nxt})
        else:
            self.send_error(404)

    def do_POST(self):
        body = self._body()
        if self.path == "/connect":
            try:
                KNOB.connect(body["port"])
                self._json({"ok": True})
            except Exception as e:
                Knob._push(f"[baglanti hata] {e}")
                self._json({"ok": False, "error": str(e)})
        elif self.path == "/disconnect":
            KNOB.disconnect()
            Knob._push("[kapatildi]")
            self._json({"ok": True})
        elif self.path == "/send":
            KNOB.send(body.get("line", ""))
            self._json({"ok": True})
        else:
            self.send_error(404)


if __name__ == "__main__":
    srv = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    url = f"http://localhost:{PORT}"
    print(f"SmartKnob paneli: {url}  (Ctrl+C ile kapat)")
    try:
        webbrowser.open(url)
    except Exception:
        pass
    srv.serve_forever()
