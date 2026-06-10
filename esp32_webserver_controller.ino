// ============================================================
//  ESP32 – CAMERA CAR WEB CONTROLLER  v5 (Safe Motor Wiring)
//  WiFi AP + WebServer + WebSocket + mDNS
//  Access:  http://cameracar.local  OR  http://192.168.4.1
// ============================================================
//
//  ── WHY THIS VERSION ───────────────────────────────────────
//  The ENA and ENB pins on many L298N boards pass raw battery
//  voltage (9V–12V) instead of a safe 3.3V/5V signal.
//  Connecting them to ESP32 GPIO destroys the ESP32 instantly.
//
//  SOLUTION: Leave ENA & ENB jumpers ON the L298N board.
//  Speed control is achieved by PWM-ing the IN pins directly.
//  Zero risk to ESP32. Works perfectly.
//
//  ── LIBRARIES (Manage Libraries in Arduino IDE) ────────────
//  1. ESPAsyncWebServer  – by lacamera
//  2. AsyncTCP           – by dvarrel
//  3. ESP32Servo         – by Kevin Harrington
//  ESPmDNS is built-in, no install needed.
//
//  ── BOARD SETTING ──────────────────────────────────────────
//  Tools → Board → ESP32 Dev Module
//
//  ── WIRING (only 4 wires to L298N, no ENA/ENB!) ───────────
//
//  ESP32 GPIO    L298N Pin    Note
//  ──────────    ─────────    ──────────────────────────────
//  GPIO 26   →   IN1         Left  motors  (PWM forward)
//  GPIO 27   →   IN2         Left  motors  (PWM backward)
//  GPIO 12   →   IN3         Right motors  (PWM forward)
//  GPIO 13   →   IN4         Right motors  (PWM backward)
//  GND       →   GND         Common ground  ← MUST connect!
//
//  ENA jumper → stays on L298N board (do NOT wire to ESP32)
//  ENB jumper → stays on L298N board (do NOT wire to ESP32)
//
//  L298N Power:
//    Battery +  → L298N 12V terminal
//    Battery -  → L298N GND terminal
//    GND rail   → also connect to ESP32 GND  (common ground)
//    L298N 5V   → DO NOT connect to ESP32 VIN (dangerous!)
//               → Power ESP32 separately via USB or own supply
//
//  L298N Outputs:
//    OUT1 & OUT2 → Left  motors M1(+−) ∥ M2(+−) in parallel
//    OUT3 & OUT4 → Right motors M3(+−) ∥ M4(+−) in parallel
//
//  Servos:
//    GPIO 16 → Pan  servo signal wire
//    GPIO 17 → Tilt servo signal wire
//    5V ext  → Both servo VCC  (NOT ESP32 3.3V pin!)
//    GND     → Both servo GND
//
//  ── HOW SPEED CONTROL WORKS WITHOUT ENA/ENB ───────────────
//  When IN1 receives PWM signal and IN2 = 0:
//    → L298N pulses OUT1/OUT2 at the same duty cycle
//    → Motor runs at proportional speed (0–100%)
//  When IN1 = 0 and IN2 receives PWM:
//    → Motor runs in reverse at proportional speed
//  When IN1 = 0 and IN2 = 0:
//    → Active brake (motor stops immediately)
// ============================================================

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>

// ─── WiFi AP ───────────────────────────────────────────────
const char* AP_SSID   = "CameraCarAP";
const char* AP_PASS   = "cameracar123";
const char* MDNS_HOST = "cameracar";      // → http://cameracar.local

// ─── L298N IN Pins (NO ENA/ENB!) ──────────────────────────
//     PWM is applied directly to IN pins for speed control
#define IN1  26   // Left  forward  (LEDC channel 0)
#define IN2  27   // Left  backward (LEDC channel 1)
#define IN3  12   // Right forward  (LEDC channel 2)
#define IN4  13   // Right backward (LEDC channel 3)

// ─── Servo Pins ────────────────────────────────────────────
#define PAN_PIN   16
#define TILT_PIN  17

// ─── PWM config (ESP32 Arduino Core 3.x) ──────────────────
//     ledcAttach(pin, freq, resolution_bits)
//     ledcWrite(pin, duty)
#define PWM_FREQ  1000    // 1kHz – good for DC motors via IN pins
#define PWM_BITS  8       // 8-bit resolution: duty 0–255

// ─── Safety ────────────────────────────────────────────────
const unsigned long TIMEOUT_MS = 1000;
unsigned long lastPkt    = 0;
bool          everActive = false;

Servo servoPan;
Servo servoTilt;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ============================================================
//  MOTOR CONTROL  (IN-pin PWM, no ENA/ENB needed)
// ============================================================

// Hard stop both motors instantly
void motorBrake() {
  ledcWrite(IN1, 0); ledcWrite(IN2, 0);
  ledcWrite(IN3, 0); ledcWrite(IN4, 0);
}

// Left side motors: pwm 0–255, fwd true=forward false=backward
void setLeft(uint8_t pwm, bool fwd) {
  if (pwm == 0) {
    ledcWrite(IN1, 0); ledcWrite(IN2, 0);   // brake
  } else if (fwd) {
    ledcWrite(IN1, pwm); ledcWrite(IN2, 0); // forward at speed
  } else {
    ledcWrite(IN1, 0); ledcWrite(IN2, pwm); // backward at speed
  }
}

// Right side motors: same logic
void setRight(uint8_t pwm, bool fwd) {
  if (pwm == 0) {
    ledcWrite(IN3, 0); ledcWrite(IN4, 0);
  } else if (fwd) {
    ledcWrite(IN3, pwm); ledcWrite(IN4, 0);
  } else {
    ledcWrite(IN3, 0);   ledcWrite(IN4, pwm);
  }
}

// Tank drive mixer
// drive: -100 (full back) to +100 (full forward)
// steer: -100 (full left)  to +100 (full right)
void driveMotors(int drive, int steer) {
  if (drive == 0 && steer == 0) {
    motorBrake();
    return;
  }
  int L = constrain(drive + steer, -100, 100);
  int R = constrain(drive - steer, -100, 100);

  setLeft (map(abs(L), 0, 100, 0, 255), L >= 0);
  setRight(map(abs(R), 0, 100, 0, 255), R >= 0);
}

// ============================================================
//  WEBSOCKET HANDLER
// ============================================================
void onWsEvent(AsyncWebSocket* s, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected  %s\n",
                  client->id(), client->remoteIP().toString().c_str());

  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected\n", client->id());
    motorBrake();

  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!(info->final && info->index == 0 &&
          info->len == len && info->opcode == WS_TEXT)) return;

    char buf[80] = {0};
    memcpy(buf, data, min(len, (size_t)79));

    // Ping/pong for latency display in browser
    if (strncmp(buf, "PING:", 5) == 0) {
      client->text(String("PONG:") + (buf + 5));
      return;
    }

    // Control packet: "drive,steer,pan,tilt"
    int drive, steer, pan, tilt;
    if (sscanf(buf, "%d,%d,%d,%d", &drive, &steer, &pan, &tilt) == 4) {
      lastPkt    = millis();
      everActive = true;

      driveMotors(constrain(drive, -100, 100),
                  constrain(steer, -100, 100));

      servoPan.write (constrain(pan,  30, 150));
      servoTilt.write(constrain(tilt, 40, 140));
    }
  }
}

// ============================================================
//  HTML  PAGE  (FPV control interface, embedded in firmware)
// ============================================================
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Camera Car</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;-webkit-user-select:none;user-select:none;touch-action:none;-webkit-tap-highlight-color:transparent}
:root{--bg:#04090e;--panel:#071525;--border:#0d2035;--cyan:#00d4ff;--green:#00ff88;--orange:#ff8c00;--red:#ff3344;--text:#90b8d8;--dim:#1e3a52;--joy:#050f18}
html,body{height:100%;overflow:hidden}
body{background:var(--bg);color:var(--text);font-family:'Courier New',monospace;display:flex;flex-direction:column}
#hdr{height:42px;flex-shrink:0;display:flex;align-items:center;gap:10px;padding:0 12px;background:var(--panel);border-bottom:1px solid var(--border)}
.logo{font-size:13px;font-weight:700;letter-spacing:3px;color:var(--cyan);text-transform:uppercase;white-space:nowrap}
.badges{display:flex;gap:5px;flex:1;overflow:hidden}
.badge{display:flex;align-items:center;gap:4px;background:#040d16;border:1px solid var(--border);border-radius:3px;padding:3px 7px;font-size:10px;letter-spacing:.5px;white-space:nowrap}
.bv{color:var(--green);font-weight:700;min-width:22px;display:inline-block;text-align:right}
#dot{width:7px;height:7px;border-radius:50%;background:var(--red);transition:background .3s;flex-shrink:0}
#dot.on{background:var(--green);box-shadow:0 0 7px var(--green)}
#stopbtn{margin-left:auto;flex-shrink:0;font-family:inherit;font-size:10px;font-weight:700;letter-spacing:1.5px;text-transform:uppercase;cursor:pointer;border-radius:3px;padding:6px 11px;background:#1e0505;border:1.5px solid var(--red);color:var(--red);transition:all .15s}
#stopbtn.hit{background:var(--red);color:#fff}
#vid{flex:1;min-height:0;position:relative;background:#000;overflow:hidden}
#vimg{width:100%;height:100%;object-fit:contain;display:block}
#vcam-off{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px;color:var(--dim);font-size:11px;letter-spacing:1px}
#vcam-off svg{width:40px;height:40px;stroke:var(--dim);fill:none;stroke-width:1.2}
.hud{position:absolute;pointer-events:none;font-size:10px;letter-spacing:.5px;line-height:2;background:rgba(4,9,14,.78);border:1px solid rgba(0,212,255,.1);border-radius:4px;padding:5px 9px;backdrop-filter:blur(5px)}
#hud-tl{top:8px;left:8px}
#hud-br{bottom:8px;right:8px;text-align:right}
.hl{color:var(--dim);font-size:9px}
.hv{color:var(--cyan);font-weight:700}
#vid::after{content:'';position:absolute;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,0,0,.05) 2px,rgba(0,0,0,.05) 4px)}
#spdbar{position:absolute;left:9px;top:50%;transform:translateY(-50%);pointer-events:none;display:flex;flex-direction:column;align-items:center;gap:3px}
#spdbar .sl{font-size:8px;letter-spacing:1px;color:var(--dim)}
#sc{border-radius:3px}
#dirpad{position:absolute;right:9px;top:50%;transform:translateY(-50%);pointer-events:none}
#ctrl{height:156px;flex-shrink:0;display:flex;align-items:center;justify-content:space-between;padding:10px 12px;background:var(--panel);border-top:1px solid var(--border);gap:8px}
.jw{display:flex;flex-direction:column;align-items:center;gap:4px;flex-shrink:0}
.jl{font-size:8px;letter-spacing:2px;text-transform:uppercase;color:var(--dim)}
.jb{width:130px;height:130px;border-radius:50%;background:var(--joy);border:1.5px solid var(--border);position:relative}
.jb::before,.jb::after{content:'';position:absolute;background:rgba(0,212,255,.05)}
.jb::before{width:1px;height:100%;left:50%;transform:translateX(-50%)}
.jb::after{width:100%;height:1px;top:50%;transform:translateY(-50%)}
.jr{position:absolute;border-radius:50%;border:1px solid rgba(0,212,255,.07);top:50%;left:50%;transform:translate(-50%,-50%)}
.jk{width:46px;height:46px;border-radius:50%;position:absolute;pointer-events:none;background:radial-gradient(circle at 35% 35%,#185570,#071e30);border:2px solid var(--cyan);box-shadow:0 0 12px rgba(0,212,255,.3),inset 0 1px 0 rgba(255,255,255,.08)}
.jk.active{box-shadow:0 0 22px rgba(0,212,255,.65)}
#ctr{flex:1;min-width:0;display:flex;flex-direction:column;align-items:center;gap:7px}
.dg{display:grid;grid-template-columns:1fr 1fr;gap:4px 8px;width:100%}
.dc{background:#040d16;border:1px solid var(--border);border-radius:3px;padding:5px 7px}
.dc .dl{font-size:8px;letter-spacing:1.5px;text-transform:uppercase;color:var(--dim)}
.dc .dv{font-size:15px;font-weight:700;color:var(--cyan);line-height:1.1}
.brow{display:flex;gap:5px;justify-content:center;width:100%}
.cbtn{font-family:inherit;font-size:9px;font-weight:700;letter-spacing:1px;text-transform:uppercase;cursor:pointer;border-radius:3px;padding:7px 10px;border:1.5px solid;transition:all .12s;white-space:nowrap}
.cbtn:active{opacity:.55}
#cenbtn{background:#040d16;border-color:var(--dim);color:var(--dim)}
#cenbtn:active{color:var(--cyan);border-color:var(--cyan)}
#turbobtn{background:#120800;border-color:var(--orange);color:var(--orange)}
#turbobtn.on{background:var(--orange);color:#000}
</style>
</head>
<body>

<div id="hdr">
  <div class="logo">&#9632; CAR-01</div>
  <div class="badges">
    <div class="badge"><div id="dot"></div>&nbsp;<span id="wstxt">OFFLINE</span></div>
    <div class="badge"><span class="hl">PING</span>&nbsp;<span class="bv" id="bping">---</span>ms</div>
    <div class="badge"><span class="hl">SPD</span>&nbsp;<span class="bv" id="bspd">0</span>%</div>
    <div class="badge"><span class="hl">PAN</span>&nbsp;<span class="bv" id="bpan">90</span>&deg;</div>
    <div class="badge"><span class="hl">TILT</span>&nbsp;<span class="bv" id="btilt">90</span>&deg;</div>
  </div>
  <button id="stopbtn">&#9632; STOP</button>
</div>

<div id="vid">
  <img id="vimg" alt="stream" style="display:none">
  <div id="vcam-off">
    <svg viewBox="0 0 24 24"><rect x="2" y="6" width="20" height="14" rx="2"/><circle cx="12" cy="13" r="4"/><path d="M8 6V4l2-1h4l2 1v2"/></svg>
    WAITING FOR CAMERA &middot; 192.168.4.3
  </div>
  <div class="hud" id="hud-tl">
    <div><span class="hl">DRIVE </span><span class="hv" id="hdrv">+0</span>%</div>
    <div><span class="hl">STEER </span><span class="hv" id="hstr">+0</span>%</div>
    <div><span class="hl">MODE  </span><span class="hv" id="hmode">STOPPED</span></div>
  </div>
  <div class="hud" id="hud-br">
    <div><span class="hl">CAM  </span><span class="hv" id="hcam">CONNECTING</span></div>
    <div><span class="hl">PAN  </span><span class="hv" id="hpan">90&deg;</span></div>
    <div><span class="hl">TILT </span><span class="hv" id="htilt">90&deg;</span></div>
  </div>
  <div id="spdbar">
    <div class="sl">SPD</div>
    <canvas id="sc" width="14" height="88"></canvas>
    <div class="sl" id="spdpct">0%</div>
  </div>
  <div id="dirpad"><canvas id="dc2" width="58" height="58"></canvas></div>
</div>

<div id="ctrl">
  <div class="jw">
    <div class="jl">Drive</div>
    <div class="jb" id="joyL">
      <div class="jr" style="width:82px;height:82px"></div>
      <div class="jr" style="width:40px;height:40px"></div>
      <div class="jk" id="knobL"></div>
    </div>
  </div>
  <div id="ctr">
    <div class="dg">
      <div class="dc"><div class="dl">Drive</div><div class="dv" id="cd">+0%</div></div>
      <div class="dc"><div class="dl">Steer</div><div class="dv" id="cs">+0%</div></div>
      <div class="dc"><div class="dl">Pan</div><div class="dv" id="cp">90&deg;</div></div>
      <div class="dc"><div class="dl">Tilt</div><div class="dv" id="ct">90&deg;</div></div>
    </div>
    <div class="brow">
      <button class="cbtn" id="cenbtn">&#8635; CENTRE</button>
      <button class="cbtn" id="turbobtn">&#9889; TURBO</button>
    </div>
  </div>
  <div class="jw">
    <div class="jl">Camera</div>
    <div class="jb" id="joyR">
      <div class="jr" style="width:82px;height:82px"></div>
      <div class="jr" style="width:40px;height:40px"></div>
      <div class="jk" id="knobR"></div>
    </div>
  </div>
</div>

<script>
// ── State ─────────────────────────────────────────────────
let drv=0,str=0,pan=90,tilt=90;
let panRate=0,tiltRate=0;
let stopped=true,turbo=false;

// ── WebSocket ─────────────────────────────────────────────
const dot=document.getElementById('dot');
const wstxt=document.getElementById('wstxt');
const stopBtn=document.getElementById('stopbtn');
let sock;

function wsConnect(){
  sock=new WebSocket('ws://'+location.hostname+'/ws');
  sock.onopen=()=>{
    dot.className='on';wstxt.textContent='ONLINE';
    stopped=true;stopBtn.classList.add('hit');
    pingLoop();
  };
  sock.onclose=()=>{
    dot.className='';wstxt.textContent='OFFLINE';
    document.getElementById('bping').textContent='---';
    setTimeout(wsConnect,1500);
  };
  sock.onerror=()=>sock.close();
  sock.onmessage=e=>{
    if(e.data.startsWith('PONG:')){
      document.getElementById('bping').textContent=Date.now()-parseInt(e.data.slice(5));
    }
  };
}
function pingLoop(){
  if(!sock||sock.readyState!==1)return;
  sock.send('PING:'+Date.now());
  setTimeout(pingLoop,1000);
}
wsConnect();

// ── Main loop @ 20Hz ──────────────────────────────────────
const DEAD=8;
setInterval(()=>{
  if(Math.abs(panRate)>DEAD)  pan =Math.max(30, Math.min(150,pan +panRate *0.10));
  if(Math.abs(tiltRate)>DEAD) tilt=Math.max(40, Math.min(140,tilt+tiltRate*0.08));
  const scale=turbo?100:65;
  const sD=stopped?0:Math.round(drv*scale/100);
  const sS=stopped?0:Math.round(str*scale/100);
  const sP=Math.round(pan);
  const sT=Math.round(tilt);
  if(sock&&sock.readyState===1) sock.send(sD+','+sS+','+sP+','+sT);
  const sg=v=>(v>=0?'+':'')+v;
  document.getElementById('hdrv').textContent=sg(sD);
  document.getElementById('hstr').textContent=sg(sS);
  document.getElementById('hpan').textContent=sP+'\u00b0';
  document.getElementById('htilt').textContent=sT+'\u00b0';
  document.getElementById('hmode').textContent=stopped?'STOPPED':(turbo?'TURBO':'NORMAL');
  document.getElementById('bspd').textContent=Math.abs(sD);
  document.getElementById('bpan').textContent=sP;
  document.getElementById('btilt').textContent=sT;
  document.getElementById('cd').textContent=sg(sD)+'%';
  document.getElementById('cs').textContent=sg(sS)+'%';
  document.getElementById('cp').textContent=sP+'\u00b0';
  document.getElementById('ct').textContent=sT+'\u00b0';
  drawSpd(Math.abs(sD));
  drawDir(sD,sS);
},50);

// ── Canvas gauges ─────────────────────────────────────────
const sc=document.getElementById('sc').getContext('2d');
const dc2=document.getElementById('dc2').getContext('2d');
function drawSpd(spd){
  const w=14,h=88;
  sc.clearRect(0,0,w,h);
  sc.fillStyle='rgba(0,212,255,.04)';sc.fillRect(0,0,w,h);
  if(spd>0){
    const f=Math.round(spd/100*h);
    const g=sc.createLinearGradient(0,h,0,0);
    g.addColorStop(0,'#00ff88');g.addColorStop(.5,'#00d4ff');g.addColorStop(1,'#ff8c00');
    sc.fillStyle=g;sc.fillRect(0,h-f,w,f);
  }
  document.getElementById('spdpct').textContent=spd+'%';
}
function drawDir(d,s){
  const sz=58,cx=29,cy=29,r=22;
  dc2.clearRect(0,0,sz,sz);
  dc2.beginPath();dc2.arc(cx,cy,r,0,Math.PI*2);
  dc2.strokeStyle='rgba(0,212,255,.12)';dc2.lineWidth=1.5;dc2.stroke();
  dc2.strokeStyle='rgba(0,212,255,.07)';dc2.lineWidth=1;
  dc2.beginPath();dc2.moveTo(cx,cy-r);dc2.lineTo(cx,cy+r);dc2.stroke();
  dc2.beginPath();dc2.moveTo(cx-r,cy);dc2.lineTo(cx+r,cy);dc2.stroke();
  if(d===0&&s===0){
    dc2.beginPath();dc2.arc(cx,cy,3,0,Math.PI*2);
    dc2.fillStyle='rgba(0,212,255,.25)';dc2.fill();return;
  }
  const mag=Math.sqrt(d*d+s*s)/100;
  const angle=Math.atan2(s,-d)-Math.PI/2;
  const len=Math.min(r-5,r*mag);
  const ex=cx+Math.cos(angle)*len,ey=cy+Math.sin(angle)*len;
  dc2.beginPath();dc2.moveTo(cx,cy);dc2.lineTo(ex,ey);
  dc2.strokeStyle='#00d4ff';dc2.lineWidth=2.5;dc2.lineCap='round';dc2.stroke();
  const hl=7,a1=angle+2.5,a2=angle-2.5;
  dc2.beginPath();
  dc2.moveTo(ex,ey);dc2.lineTo(ex+Math.cos(a1)*hl,ey+Math.sin(a1)*hl);
  dc2.moveTo(ex,ey);dc2.lineTo(ex+Math.cos(a2)*hl,ey+Math.sin(a2)*hl);
  dc2.stroke();
}

// ── Virtual joystick ──────────────────────────────────────
function Joystick(baseEl,knobEl,onMove,onRelease){
  const BR=65,KR=23,MAX=BR-KR;
  let active=false;
  function place(dx,dy){
    knobEl.style.left=(BR+dx-KR)+'px';
    knobEl.style.top=(BR+dy-KR)+'px';
  }
  function centre(){place(0,0);knobEl.classList.remove('active');onRelease();}
  function move(px,py){
    const rc=baseEl.getBoundingClientRect();
    let dx=px-(rc.left+BR),dy=py-(rc.top+BR);
    const d=Math.sqrt(dx*dx+dy*dy);
    if(d>MAX){dx*=MAX/d;dy*=MAX/d;}
    place(dx,dy);knobEl.classList.add('active');
    onMove(Math.round(dx/MAX*100),Math.round(dy/MAX*100));
  }
  baseEl.addEventListener('touchstart',e=>{e.preventDefault();active=true;move(e.targetTouches[0].clientX,e.targetTouches[0].clientY);},{passive:false});
  baseEl.addEventListener('touchmove', e=>{e.preventDefault();if(active)move(e.targetTouches[0].clientX,e.targetTouches[0].clientY);},{passive:false});
  baseEl.addEventListener('touchend',  e=>{e.preventDefault();active=false;centre();},{passive:false});
  baseEl.addEventListener('mousedown', e=>{active=true;move(e.clientX,e.clientY);});
  window.addEventListener('mousemove', e=>{if(active)move(e.clientX,e.clientY);});
  window.addEventListener('mouseup',   ()=>{if(active){active=false;centre();}});
  centre();
}

Joystick(
  document.getElementById('joyL'),
  document.getElementById('knobL'),
  (x,y)=>{
    str=x; drv=-y;
    if(stopped&&(Math.abs(x)>5||Math.abs(y)>5)){
      stopped=false;stopBtn.classList.remove('hit');
    }
  },
  ()=>{drv=0;str=0;}
);

Joystick(
  document.getElementById('joyR'),
  document.getElementById('knobR'),
  (x,y)=>{panRate=x;tiltRate=y;},
  ()=>{panRate=0;tiltRate=0;}
);

// ── Buttons ───────────────────────────────────────────────
stopBtn.addEventListener('pointerdown',e=>{
  e.preventDefault();
  stopped=true;drv=0;str=0;
  stopBtn.classList.add('hit');
  if(navigator.vibrate)navigator.vibrate([40,20,40]);
});
document.getElementById('cenbtn').addEventListener('pointerdown',e=>{
  e.preventDefault();
  pan=90;tilt=90;panRate=0;tiltRate=0;
});
document.getElementById('turbobtn').addEventListener('pointerdown',e=>{
  e.preventDefault();
  turbo=!turbo;
  document.getElementById('turbobtn').classList.toggle('on',turbo);
  if(navigator.vibrate)navigator.vibrate(turbo?[20,10,20,10,40]:30);
});

// ── Camera stream ─────────────────────────────────────────
const CAM='http://192.168.4.3/stream';
const vimg=document.getElementById('vimg');
const voff=document.getElementById('vcam-off');
const hcam=document.getElementById('hcam');
function loadCam(){
  vimg.style.display='block';
  vimg.src=CAM+'?t='+Date.now();
  hcam.textContent='CONNECTING';
}
vimg.onload =()=>{voff.style.display='none';hcam.textContent='LIVE';};
vimg.onerror=()=>{
  vimg.style.display='none';voff.style.display='flex';
  hcam.textContent='OFFLINE';
  setTimeout(loadCam,3000);
};
loadCam();
drawSpd(0);drawDir(0,0);
</script>
</body>
</html>
)rawliteral";

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n==============================");
  Serial.println("  Camera Car v5 – Safe Motor");
  Serial.println("==============================");

  // ── Step 1: Attach LEDC to all 4 IN pins FIRST
  //    All pins start at duty=0 → motors cannot move at boot
  ledcAttach(IN1, PWM_FREQ, PWM_BITS); ledcWrite(IN1, 0);
  ledcAttach(IN2, PWM_FREQ, PWM_BITS); ledcWrite(IN2, 0);
  ledcAttach(IN3, PWM_FREQ, PWM_BITS); ledcWrite(IN3, 0);
  ledcAttach(IN4, PWM_FREQ, PWM_BITS); ledcWrite(IN4, 0);
  Serial.println("[MOTOR] PWM on IN1-IN4, duty=0. Stopped.");
  Serial.println("[MOTOR] ENA/ENB: jumpers on L298N board (not wired to ESP32)");

  // ── Step 2: Servos
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoPan.setPeriodHertz(50);
  servoTilt.setPeriodHertz(50);
  servoPan.attach (PAN_PIN,  500, 2400);
  servoTilt.attach(TILT_PIN, 500, 2400);
  servoPan.write(90);
  servoTilt.write(90);
  Serial.println("[SERVO] Pan=90  Tilt=90");

  // ── Step 3: WiFi AP
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WIFI]  AP \"%s\"  IP: %s\n", AP_SSID, ip.toString().c_str());

  // ── Step 4: mDNS → http://cameracar.local
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS]  http://%s.local\n", MDNS_HOST);
  } else {
    Serial.println("[mDNS]  Failed – use IP");
  }

  // ── Step 5: WebSocket + HTTP
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", HTML);
  });
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", "OK");
  });
  server.begin();

  Serial.println("[HTTP]  Server running");
  Serial.println("──────────────────────────");
  Serial.printf("  Open: http://%s.local\n", MDNS_HOST);
  Serial.printf("  or:   http://%s\n", ip.toString().c_str());
  Serial.println("  WiFi: " + String(AP_SSID) + " / " + String(AP_PASS));
  Serial.println("──────────────────────────");

  lastPkt = millis();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  ws.cleanupClients();

  // Safety: brake if no websocket packet for TIMEOUT_MS
  if (everActive && (millis() - lastPkt > TIMEOUT_MS)) {
    motorBrake();
  }

  delay(10);
}
