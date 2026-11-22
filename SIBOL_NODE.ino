/* SIBOL NODE · ESP32 + MQ-135 + SSD1306 0.96" (128x64) + 3.7 V Li-ion
   POWER RULE: 3.7 V cell -> 5 V boost -> ESP32 VIN and MQ-135 heater.
   Never put 3.7 V on the 3V3 pin (abs max 3.6 V) or on VIN (LDO dropout brownout).
   GPIO32 senses the heater rail through 100k/100k so the node can flag undervoltage.
   Libs: ESP32 Arduino core, Adafruit SSD1306 + Adafruit GFX */
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define PIN_SOIL 35
#define PIN_MQ   34
#define PIN_BAT  33
#define PIN_RAIL 32
#define PIN_LED  2
#define MAXN 63
#define LOGN 240
#define MAXC 8
#define HEATER_OHM 31.0f

Adafruit_SSD1306 oled(128,64,&Wire,-1);
WebServer server(80); DNSServer dns; Preferences prefs;
const char* SSID="SIBOL-NODE";
const char* FW="1.1";
const char* SENSOR="MQ-135";
IPAddress ip(192,168,4,1), mask(255,255,255,0);
char NODE_ID[20]="SIBOL-01";
char PAIR_CODE[5]="0000";

struct { int dry=2600,wet=1100,avg=16,med=3,vote=5,period=1000,warm=60,mah=2600,draw=400,rl=1000;
         float gain=2.0,ema=0.28; char src[10]="18650"; } P;

float MODEL[MAXN][6]={{0,0,40,1,4,0},{0,1,1500,2,3,0},{1,0,0,0,0,1},{1,0,0,0,0,2},
{0,1,1500,5,8,0},{0,0,85,6,7,0},{1,0,0,0,0,0},{1,0,0,0,0,1},{1,0,0,0,0,2}};
int modelN=9;

struct Peer { char tok[17]; char name[15]; uint32_t seen; bool used; };
Peer clients[MAXC];

float soilMv=0,mqMv=0,batMv=0,railMv=0,rsOhm=0,r0Ohm=0;
float mS[9],mM[9]; int mIdx=0; bool primed=false;
int soilPct=0,grade=-1,stable=-1,streak=0,lastG=-1,vhState=0;
uint32_t inferUs=0,tSamp=0,tOled=0,warmEnd=0,burn=0,ackUntil=0;
char ackName[15]="";
uint16_t lgS[LOGN],lgM[LOGN]; uint8_t lgG[LOGN]; uint32_t lgT[LOGN]; int lgH=0,lgC=0;
bool oledOk=false,fsOk=false;
const char* HDRS[]={"X-SIBOL-Token"};

const float CV[11]={4.20,4.10,4.00,3.90,3.80,3.70,3.60,3.50,3.40,3.30,3.00};
const int   CP[11]={100, 90,  80,  68,  55,  42,  30,  20,  12,  6,   0};

float rdMV(int p,int n){uint32_t s=0;for(int i=0;i<n;i++){s+=analogReadMilliVolts(p);delayMicroseconds(180);}return (float)s/n;}
float med(float*a,int n){float t[9];for(int i=0;i<n;i++)t[i]=a[i];
  for(int i=1;i<n;i++){float k=t[i];int j=i-1;while(j>=0&&t[j]>k){t[j+1]=t[j];j--;}t[j+1]=k;}return t[n/2];}
int predict(const float*x){int i=0,g=0;while(MODEL[i][0]==0.0f&&g++<MAXN){
  i=(x[(int)MODEL[i][1]]<=MODEL[i][2])?(int)MODEL[i][3]:(int)MODEL[i][4];if(i<0||i>=modelN)return 1;}return (int)MODEL[i][5];}
int batPct(){
  if(!strcmp(P.src,"usb"))return 100;
  float v=batMv/1000.0f;
  if(v<1.0f)return 100;
  if(v>=CV[0])return 100;
  if(v<=CV[10])return 0;
  for(int i=0;i<10;i++){
    if(v<=CV[i]&&v>=CV[i+1]){
      float f=(CV[i]-v)/(CV[i]-CV[i+1]);
      return (int)(CP[i]+f*(CP[i+1]-CP[i]));
    }}
  return 0;}
void cors(){server.sendHeader("Access-Control-Allow-Origin","*");
  server.sendHeader("Access-Control-Allow-Headers","Content-Type, X-SIBOL-Token");
  server.sendHeader("Access-Control-Allow-Methods","GET,POST,OPTIONS");}
int clientCount(){int n=0;for(int i=0;i<MAXC;i++)if(clients[i].used)n++;return n;}
int findTok(const String& t){if(t.length()!=16)return -1;
  for(int i=0;i<MAXC;i++)if(clients[i].used&&t.equals(clients[i].tok))return i;return -1;}
bool authed(){return findTok(server.header("X-SIBOL-Token"))>=0;}
void touch(){int i=findTok(server.header("X-SIBOL-Token"));if(i>=0)clients[i].seen=millis();}

void loadPrefs(){prefs.begin("sibol",true);
  P.dry=prefs.getInt("dry",P.dry);P.wet=prefs.getInt("wet",P.wet);P.avg=prefs.getInt("avg",P.avg);
  P.med=prefs.getInt("med",P.med);P.vote=prefs.getInt("vote",P.vote);P.period=prefs.getInt("period",P.period);
  P.warm=prefs.getInt("warm",P.warm);P.gain=prefs.getFloat("gain",P.gain);P.ema=prefs.getFloat("ema",P.ema);
  P.mah=prefs.getInt("mah",P.mah);P.draw=prefs.getInt("draw",P.draw);P.rl=prefs.getInt("rl",P.rl);
  r0Ohm=prefs.getFloat("r0",0);
  prefs.getString("src","18650").toCharArray(P.src,10); burn=prefs.getUInt("burn",0);
  String code=prefs.getString("code","");
  if(code.length()!=4){uint64_t m=ESP.getEfuseMac();code=String((uint32_t)((m^(m>>21))%10000));
    while(code.length()<4)code="0"+code;}
  code.toCharArray(PAIR_CODE,5);
  String id=prefs.getString("id","");
  if(id.length()<3){id="SIBOL-"+String((uint32_t)(ESP.getEfuseMac()&0xFFF),HEX);id.toUpperCase();}
  id.toCharArray(NODE_ID,20);
  String blob=prefs.getString("cli","");
  int st=0,idx=0;
  while(st<(int)blob.length()&&idx<MAXC){
    int se=blob.indexOf(';',st); if(se<0)se=blob.length();
    String rec=blob.substring(st,se); int c=rec.indexOf(':');
    if(c==16){clients[idx].used=true;
      rec.substring(0,16).toCharArray(clients[idx].tok,17);
      rec.substring(c+1).toCharArray(clients[idx].name,15);
      clients[idx].seen=0; idx++;}
    st=se+1;}
  int n=prefs.getInt("mn",0);
  if(n>0&&n<=MAXN){modelN=n;for(int i=0;i<n;i++)for(int j=0;j<6;j++){char k[8];snprintf(k,8,"m%d_%d",i,j);
    MODEL[i][j]=prefs.getFloat(k,MODEL[i][j]);}}
  prefs.end();}
void saveParams(){prefs.begin("sibol",false);
  prefs.putInt("dry",P.dry);prefs.putInt("wet",P.wet);prefs.putInt("avg",P.avg);prefs.putInt("med",P.med);
  prefs.putInt("vote",P.vote);prefs.putInt("period",P.period);prefs.putInt("warm",P.warm);
  prefs.putFloat("gain",P.gain);prefs.putFloat("ema",P.ema);prefs.putInt("mah",P.mah);
  prefs.putInt("draw",P.draw);prefs.putInt("rl",P.rl);prefs.putFloat("r0",r0Ohm);
  prefs.putString("src",P.src);prefs.putString("code",PAIR_CODE);prefs.putString("id",NODE_ID);prefs.end();}
void saveModel(){prefs.begin("sibol",false);prefs.putInt("mn",modelN);
  for(int i=0;i<modelN;i++)for(int j=0;j<6;j++){char k[8];snprintf(k,8,"m%d_%d",i,j);prefs.putFloat(k,MODEL[i][j]);}prefs.end();}
void saveClients(){String b="";
  for(int i=0;i<MAXC;i++)if(clients[i].used){b+=clients[i].tok;b+=":";b+=clients[i].name;b+=";";}
  prefs.begin("sibol",false);prefs.putString("cli",b);prefs.end();}

void sample(){
  int n=constrain(P.avg,1,32);
  float sRaw=rdMV(PIN_SOIL,n);
  float qRaw=rdMV(PIN_MQ,n)*P.gain;
  batMv=rdMV(PIN_BAT,8)*2.0f;
  railMv=rdMV(PIN_RAIL,8)*2.0f;

  if(railMv<1000) vhState=0;
  else if(railMv>=4600) vhState=1;
  else vhState=2;

  int m=constrain(P.med,1,9);
  mS[mIdx]=sRaw;mM[mIdx]=qRaw;mIdx=(mIdx+1)%m;
  float s=med(mS,m),q=med(mM,m);
  if(!primed){soilMv=s;mqMv=q;primed=true;} else {soilMv+=P.ema*(s-soilMv);mqMv+=P.ema*(q-mqMv);}
  soilPct=(int)constrain((soilMv-P.dry)/(float)(P.wet-P.dry)*100.0f,0,100);

  float vc=(vhState==0)?5000.0f:railMv;
  if(mqMv>10.0f && vc>mqMv) rsOhm=(float)P.rl*(vc-mqMv)/mqMv; else rsOhm=0;

  float x[2]={(float)soilPct,mqMv};
  uint32_t t0=micros(); grade=predict(x); inferUs=micros()-t0;
  streak=(grade==lastG)?streak+1:1; lastG=grade; if(streak>=P.vote) stable=grade;

  lgS[lgH]=soilPct;lgM[lgH]=(uint16_t)constrain(mqMv,0,65535);lgG[lgH]=stable<0?255:stable;lgT[lgH]=millis();
  lgH=(lgH+1)%LOGN; if(lgC<LOGN)lgC++;

  if(millis()<ackUntil) digitalWrite(PIN_LED,(millis()/90)%2);
  else if(vhState==2) digitalWrite(PIN_LED,(millis()/200)%2);
  else digitalWrite(PIN_LED, stable==2?(millis()/120)%2:(stable==1?(millis()/500)%2:(millis()/2000)%2));
}
void drawOled(){ if(!oledOk)return;
  static const char* GK[3]={"A","B","C"}; static const char* GN[3]={"SARIWA","ALAGA","PIGIL"};
  oled.clearDisplay();oled.setTextColor(SSD1306_WHITE);oled.setTextSize(1);
  if(millis()<ackUntil){
    oled.setCursor(0,0);oled.print("SIBOL-NODE");
    oled.drawFastHLine(0,10,128,SSD1306_WHITE);
    oled.setTextSize(2);oled.setCursor(0,16);oled.print("LINKED");
    oled.setTextSize(1);oled.setCursor(0,36);oled.print(ackName);
    oled.setCursor(0,48);oled.print(clientCount());oled.print(" paired device(s)");
    oled.display();return;
  }
  oled.setCursor(0,0);oled.print("SIBOL-NODE");
  oled.setCursor(96,0);oled.print(clientCount());oled.print("pr");
  oled.drawFastHLine(0,10,128,SSD1306_WHITE);
  oled.setCursor(0,13);oled.print("IP ");oled.print(WiFi.softAPIP().toString());
  oled.setCursor(0,23);oled.print("CODE ");oled.print(PAIR_CODE);
  oled.setCursor(0,33);
  oled.print("S:");oled.print(soilPct);oled.print("% A:");oled.print((int)mqMv);
  if(vhState==0){oled.print(" V:--");}
  else{oled.print(" V:");oled.print(railMv/1000.0f,1);if(vhState==2&&(millis()/600)%2)oled.print("!");}
  oled.drawFastHLine(0,43,128,SSD1306_WHITE);
  if(millis()<warmEnd){oled.setCursor(0,49);oled.print("WARM-UP ");oled.print((warmEnd-millis())/1000);oled.print("s");}
  else if(stable>=0){oled.setTextSize(2);oled.setCursor(0,46);oled.print(GK[stable]);
    oled.setTextSize(1);oled.setCursor(22,50);oled.print(GN[stable]);
    oled.setCursor(100,52);oled.print(batPct());oled.print("%");}
  else {oled.setCursor(0,49);oled.print("SETTLING");}
  oled.display();}

const char* BOOT="<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<style>body{background:#0A1310;color:#F2EFE6;font-family:system-ui;padding:30px;max-width:600px;margin:auto}"
"h1{color:#C9F75C;font-family:Georgia,serif}code{color:#37E0FF}input,button{font:inherit;width:100%;padding:11px;margin-top:10px;"
"border-radius:9px;border:1px solid #2F5A45;background:#13251D;color:#F2EFE6}button{background:#1FC77A;color:#07130C;font-weight:800;border:0}</style>"
"<h1>SIBOL node is alive</h1><p>Read <code>IP</code> and <code>CODE</code> on the OLED, then pair in the app.</p>"
"<p>Upload SIBOL.html here, or use Setup &rarr; Send app to node.</p>"
"<form method=POST action=/upload enctype=multipart/form-data><input type=file name=app required><button>Store</button></form>";

String ctype(const String& p){
  if(p.endsWith(".html"))return "text/html";
  if(p.endsWith(".js"))return "text/javascript";
  if(p.endsWith(".webmanifest"))return "application/manifest+json";
  if(p.endsWith(".json"))return "application/json";
  if(p.endsWith(".svg"))return "image/svg+xml";
  if(p.endsWith(".png"))return "image/png";
  if(p.endsWith(".ico"))return "image/x-icon";
  return "text/plain";}
void sendFile(const String& p){
  if(fsOk&&LittleFS.exists(p)){File f=LittleFS.open(p,"r");
    server.sendHeader("Cache-Control","no-cache");server.streamFile(f,ctype(p));f.close();return;}
  server.send(404,"text/plain","missing");}
void serveApp(){cors();
  if(fsOk&&LittleFS.exists("/index.html"))sendFile("/index.html");
  else server.send(200,"text/html",BOOT);}
void serveManifest(){cors();
  String m="{\"name\":\"SIBOL — Offline Node\",\"short_name\":\"SIBOL\",\"start_url\":\"./\","
           "\"scope\":\"./\",\"display\":\"standalone\",\"background_color\":\"#0A1310\","
           "\"theme_color\":\"#0A1310\",\"lang\":\"en\"";
  if(fsOk&&LittleFS.exists("/logo.png"))
    m+=",\"icons\":[{\"src\":\"logo.png\",\"sizes\":\"512x512\",\"type\":\"image/png\",\"purpose\":\"any maskable\"}]";
  m+="}";server.send(200,"application/manifest+json",m);}
void home(){cors();server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}

void apiPair(){cors();
  String code=server.arg("code"), name=server.arg("name");
  code.trim(); name.trim(); if(name.length()==0)name="Device"; name=name.substring(0,14);
  if(code!=String(PAIR_CODE)){server.send(200,"application/json","{\"ok\":false,\"err\":\"code\"}");return;}
  int slot=-1;
  for(int i=0;i<MAXC;i++)if(clients[i].used&&name.equals(clients[i].name)){slot=i;break;}
  if(slot<0)for(int i=0;i<MAXC;i++)if(!clients[i].used){slot=i;break;}
  if(slot<0)slot=0;
  char tok[17]; for(int i=0;i<16;i++){uint8_t v=esp_random()&15;tok[i]="0123456789abcdef"[v];} tok[16]=0;
  clients[slot].used=true; strcpy(clients[slot].tok,tok); name.toCharArray(clients[slot].name,15);
  clients[slot].seen=millis(); saveClients();
  name.toCharArray(ackName,15); ackUntil=millis()+4000;
  char b[300];snprintf(b,sizeof(b),
    "{\"ok\":true,\"node\":\"%s\",\"token\":\"%s\",\"fw\":\"%s\",\"model_n\":%d,\"ip\":\"%s\",\"paired\":%d}",
    NODE_ID,tok,FW,modelN,WiFi.softAPIP().toString().c_str(),clientCount());
  server.send(200,"application/json",b);}
void apiUnpair(){cors();
  int i=findTok(server.header("X-SIBOL-Token"));
  if(i>=0){clients[i].used=false;clients[i].tok[0]=0;saveClients();}
  server.send(200,"application/json","{\"ok\":true}");}
void apiState(){cors();touch();char b[820];
  float ratio=(r0Ohm>1.0f&&rsOhm>1.0f)?rsOhm/r0Ohm:0.0f;
  snprintf(b,sizeof(b),"{\"node\":\"%s\",\"fw\":\"%s\",\"sensor\":\"%s\",\"up\":%lu,\"soil\":%d,\"mq\":%d,"
  "\"grade\":%d,\"streak\":%d,\"us\":%lu,\"bat_pct\":%d,\"bat_mv\":%d,\"rail_mv\":%d,\"vh\":%d,"
  "\"rs\":%.0f,\"r0\":%.0f,\"ratio\":%.3f,\"rl\":%d,\"clients\":%d,\"paired\":%d,\"heap\":%u,"
  "\"burn_h\":%.2f,\"warm\":%d,\"model_n\":%d,\"ip\":\"%s\",\"ack\":\"%s\"}",
  NODE_ID,FW,SENSOR,(unsigned long)millis(),soilPct,(int)mqMv,stable,streak,(unsigned long)inferUs,
  batPct(),(int)batMv,(int)railMv,vhState,rsOhm,r0Ohm,ratio,P.rl,
  WiFi.softAPgetStationNum(),clientCount(),ESP.getFreeHeap(),burn/3600.0f,
  (int)(millis()<warmEnd?(warmEnd-millis())/1000:0),modelN,WiFi.softAPIP().toString().c_str(),
  millis()<ackUntil?ackName:"");
  server.send(200,"application/json",b);}
void apiCal(){cors(); if(!authed()){server.send(401,"application/json","{\"ok\":false}");return;}
  if(rsOhm<1.0f){server.send(200,"application/json","{\"ok\":false,\"err\":\"no signal\"}");return;}
  r0Ohm=rsOhm; saveParams();
  char b[120];snprintf(b,sizeof(b),"{\"ok\":true,\"rs\":%.0f,\"r0\":%.0f}",rsOhm,r0Ohm);
  server.send(200,"application/json",b);}
void apiParams(){cors(); if(!authed()){server.send(401,"application/json","{\"ok\":false}");return;}
  if(server.hasArg("dry"))P.dry=server.arg("dry").toInt();
  if(server.hasArg("wet"))P.wet=server.arg("wet").toInt();
  if(server.hasArg("avg"))P.avg=constrain(server.arg("avg").toInt(),1,32);
  if(server.hasArg("med"))P.med=constrain(server.arg("med").toInt(),1,9);
  if(server.hasArg("vote"))P.vote=constrain(server.arg("vote").toInt(),1,15);
  if(server.hasArg("period"))P.period=constrain(server.arg("period").toInt(),100,5000);
  if(server.hasArg("warm")){P.warm=server.arg("warm").toInt();warmEnd=millis()+P.warm*1000UL;}
  if(server.hasArg("gain"))P.gain=server.arg("gain").toFloat();
  if(server.hasArg("ema"))P.ema=constrain(server.arg("ema").toFloat(),0.02f,1.0f);
  if(server.hasArg("mah"))P.mah=server.arg("mah").toInt();
  if(server.hasArg("draw"))P.draw=server.arg("draw").toInt();
  if(server.hasArg("rl"))P.rl=constrain(server.arg("rl").toInt(),100,100000);
  if(server.hasArg("src"))server.arg("src").toCharArray(P.src,10);
  saveParams();primed=false;server.send(200,"application/json","{\"ok\":true}");}
void apiModel(){cors(); if(!authed()){server.send(401,"application/json","{\"ok\":false}");return;}
  String b=server.arg("plain");
  if(b.length()<5){server.send(400,"application/json","{\"ok\":false}");return;}
  float tmp[MAXN][6];int n=0,st=0;
  while(st<(int)b.length()&&n<MAXN){int se=b.indexOf(';',st);if(se<0)se=b.length();
    String r=b.substring(st,se);int p=0;
    for(int j=0;j<6;j++){int c=r.indexOf(',',p);tmp[n][j]=(c<0?r.substring(p):r.substring(p,c)).toFloat();p=c<0?r.length():c+1;}
    n++;st=se+1;}
  for(int i=0;i<n;i++)for(int j=0;j<6;j++)MODEL[i][j]=tmp[i][j];
  modelN=n;saveModel();streak=0;stable=-1;
  char r[48];snprintf(r,sizeof(r),"{\"ok\":true,\"nodes\":%d}",n);server.send(200,"application/json",r);}
void boardGet(){cors();touch();
  if(fsOk&&LittleFS.exists("/board.json"))sendFile("/board.json");
  else server.send(200,"application/json","[]");}
void boardPost(){cors(); if(!authed()){server.send(401,"application/json","{\"ok\":false}");return;}
  String b=server.arg("plain");
  if(!fsOk||b.length()<2){server.send(500,"application/json","{\"ok\":false}");return;}
  File f=LittleFS.open("/board.json","w");f.print(b);f.close();server.send(200,"application/json","{\"ok\":true}");}
void apiLog(){cors();server.setContentLength(CONTENT_LENGTH_UNKNOWN);server.send(200,"text/csv","");
  server.sendContent("t_ms,soil_pct,mq_mv,grade\n");char l[64];
  for(int i=0;i<lgC;i++){int k=(lgH-lgC+i+LOGN)%LOGN;
    snprintf(l,sizeof(l),"%lu,%u,%u,%d\n",(unsigned long)lgT[k],lgS[k],lgM[k],lgG[k]==255?-1:lgG[k]);
    server.sendContent(l);}
  server.sendContent("");}
void upDone(){cors();server.send(fsOk?200:500,"application/json",fsOk?"{\"ok\":true}":"{\"ok\":false}");}
void upChunk(){ if(!fsOk)return; HTTPUpload& u=server.upload(); static File f; static bool ok;
  if(u.status==UPLOAD_FILE_START){
    ok=authed()||clientCount()==0;
    String n=u.filename; int sl=n.lastIndexOf('/'); if(sl>=0)n=n.substring(sl+1);
    if(n.length()<3||n.length()>28)ok=false;
    if(ok)f=LittleFS.open("/"+n,"w");
  } else if(u.status==UPLOAD_FILE_WRITE){ if(ok&&f) f.write(u.buf,u.currentSize); }
  else if(u.status==UPLOAD_FILE_END){ if(f) f.close(); } }
void notFound(){
  if(server.method()==HTTP_OPTIONS){cors();server.send(204);return;}
  String p=server.uri();
  if(p=="/"||p=="/index.html"){serveApp();return;}
  if(fsOk&&LittleFS.exists(p)){cors();sendFile(p);return;}
  serveApp();}

void setup(){
  Serial.begin(115200); pinMode(PIN_LED,OUTPUT);
  analogSetPinAttenuation(PIN_SOIL,ADC_11db);
  analogSetPinAttenuation(PIN_MQ,ADC_11db);
  analogSetPinAttenuation(PIN_BAT,ADC_11db);
  analogSetPinAttenuation(PIN_RAIL,ADC_11db);
  Wire.begin(21,22); oledOk=oled.begin(SSD1306_SWITCHCAPVCC,0x3C);
  if(oledOk){oled.clearDisplay();oled.setTextSize(1);oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0,0);oled.println("SIBOL booting");oled.display();}
  loadPrefs(); fsOk=LittleFS.begin(true);
  WiFi.mode(WIFI_AP); WiFi.softAPConfig(ip,ip,mask); WiFi.softAP(SSID);
  dns.start(53,"*",ip);
  server.collectHeaders(HDRS,1);
  server.on("/",HTTP_GET,serveApp);
  server.on("/manifest.webmanifest",HTTP_GET,serveManifest);
  server.on("/logo.png",HTTP_GET,[]{cors();sendFile("/logo.png");});
  server.on("/api/pair",HTTP_POST,apiPair);
  server.on("/api/unpair",HTTP_POST,apiUnpair);
  server.on("/api/state",HTTP_GET,apiState);
  server.on("/api/cal",HTTP_POST,apiCal);
  server.on("/api/params",HTTP_POST,apiParams);
  server.on("/api/model",HTTP_POST,apiModel);
  server.on("/api/board",HTTP_GET,boardGet);
  server.on("/api/board",HTTP_POST,boardPost);
  server.on("/api/log",HTTP_GET,apiLog);
  server.on("/upload",HTTP_POST,upDone,upChunk);
  const char* probes[]={"/generate_204","/gen_204","/hotspot-detect.html","/library/test/success.html",
    "/connecttest.txt","/ncsi.txt","/canonical.html","/success.txt","/redirect"};
  for(auto p:probes) server.on(p,HTTP_GET,home);
  server.onNotFound(notFound); server.begin();
  warmEnd=millis()+P.warm*1000UL;
  Serial.printf("SIBOL %s · %s · http://%s · CODE %s · %s · %d nodes\n",
    NODE_ID,SSID,WiFi.softAPIP().toString().c_str(),PAIR_CODE,SENSOR,modelN);
}
void loop(){
  dns.processNextRequest(); server.handleClient();
  uint32_t now=millis();
  if(now-tSamp>=(uint32_t)P.period){tSamp=now;sample();}
  if(now-tOled>=350){tOled=now;drawOled();}
  static uint32_t tb=0;
  if(now-tb>=1000){tb=now;burn++;if(burn%600==0){prefs.begin("sibol",false);prefs.putUInt("burn",burn);prefs.end();}}
}