#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>

WebServer server(80);
TFT_eSPI tft = TFT_eSPI();

#define EEPROM_SIZE 1024
#define SWITCH_DELAY 50   // Umschaltpause in ms

// ---------------- Hardware ----------------
const int outputs[4] = {27,26,25,33};
const int inputs[4]  = {32,21,22,17};   // 5V über Spannungsteiler!

String bands[]={
"0","160m","80m","40m","30m","20m",
"17m","15m","12m","10m","6m"
};

int bandToOut[11];
int currentBand=-1;
int currentOut=-1;

// ---------------- Config ----------------
String wifiSSID="";
String wifiPASS="";
String adminPass="admin";
bool apMode=false;

// ---- DHCP / Static ----
bool useStaticIP=false;
String staticIP="";
String staticGW="";
String staticMask="";
String staticDNS="";

// ================= EEPROM =================
void saveConfig(){
  EEPROM.writeString(0,wifiSSID);
  EEPROM.writeString(64,wifiPASS);
  EEPROM.writeString(128,adminPass);
  EEPROM.writeBool(300,useStaticIP);
  EEPROM.writeString(304,staticIP);
  EEPROM.writeString(340,staticGW);
  EEPROM.writeString(380,staticMask);
  EEPROM.writeString(420,staticDNS);
  for(int i=0;i<11;i++)
    EEPROM.write(200+i,bandToOut[i]);
  EEPROM.commit();
}

void loadConfig(){
  wifiSSID=EEPROM.readString(0);
  wifiPASS=EEPROM.readString(64);
  adminPass=EEPROM.readString(128);
  if(adminPass=="") adminPass="admin";
  useStaticIP = EEPROM.readBool(300);
  staticIP   = EEPROM.readString(304);
  staticGW   = EEPROM.readString(340);
  staticMask = EEPROM.readString(380);
  staticDNS  = EEPROM.readString(420);
  for(int i=0;i<11;i++)
    bandToOut[i]=EEPROM.read(200+i);
}

// ================= BAND =================
int readBand(){
  int value=0;
  for(int i=0;i<4;i++){
    if(digitalRead(inputs[i])==HIGH)
      value |= (1<<i);
  }
  if(value>=1 && value<=10) return value;
  return -1;
}

// ================= DISPLAY =================
void drawStaticUI(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE,TFT_BLACK);

  tft.setCursor(0,0);
  tft.println("Antenna Router");
  tft.drawLine(0,22,240,22,TFT_DARKGREY);

  tft.setCursor(0,30);  tft.println("Band:");
  tft.setCursor(0,55);  tft.println("Antenne:");
  tft.setCursor(0,80);  tft.println("Ausgang:");
  tft.setCursor(0,105); tft.println("IP:");
}

void updateDisplay(){

  static int lastBand=-2;
  static int lastOut=-2;
  static String lastIP="";

  String ipStr = apMode ? WiFi.softAPIP().toString()
                        : WiFi.localIP().toString();

  if(currentBand==lastBand &&
     currentOut==lastOut &&
     ipStr==lastIP)
    return;

  lastBand=currentBand;
  lastOut=currentOut;
  lastIP=ipStr;

  tft.fillRect(100,30,130,95,TFT_BLACK);
  tft.setCursor(100,30);

  if(currentBand<1){
    tft.setTextColor(TFT_RED,TFT_BLACK);
    tft.println("---");
    tft.setCursor(100,55);
    tft.println("None!");
    tft.setCursor(100,80);
    tft.println("-");
  } else {
    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    tft.println(bands[currentBand]);
    tft.setCursor(100,55);
    tft.println("Antenne "+String(currentOut+1));
    tft.setCursor(100,80);
    tft.println(String(currentOut+1));
  }

  tft.setCursor(100,105);
  tft.setTextColor(TFT_CYAN,TFT_BLACK);
  tft.println(ipStr);
}

// ================= HF-SCHONENDES UMSCHALTEN =================
void switchOutput(int newOut){

  // Alle Relais AUS
  for(int i=0;i<4;i++)
    digitalWrite(outputs[i],LOW);

  delay(SWITCH_DELAY);

  if(newOut>=0 && newOut<4)
    digitalWrite(outputs[newOut],HIGH);
}

// ================= ROUTING =================
void applyRoute(int band){

  if(band==currentBand)
    return;

  currentBand=band;

  if(band<1){
    currentOut=-1;
    switchOutput(-1);
    updateDisplay();
    return;
  }

  currentOut=bandToOut[band];
  switchOutput(currentOut);
  updateDisplay();
}

// ================= LOGIN =================
bool loggedIn(){
  return server.hasArg("p") && server.arg("p")==adminPass;
}

// ================= WIFI =================
void startWiFi(){

  WiFi.mode(WIFI_STA);

  if(useStaticIP){
    IPAddress ip,gw,mask,dns;
    if(ip.fromString(staticIP) &&
       gw.fromString(staticGW) &&
       mask.fromString(staticMask) &&
       dns.fromString(staticDNS)){
      WiFi.config(ip,gw,mask,dns);
    }
  } else {
    WiFi.config(0U,0U,0U);  // DHCP explizit aktivieren
  }

  WiFi.begin(wifiSSID.c_str(),wifiPASS.c_str());
  WiFi.setSleep(false);

  unsigned long start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<8000){
    delay(200);
  }

  if(WiFi.status()!=WL_CONNECTED){
    WiFi.disconnect(true);
    delay(500);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("AntennaRouter");
    WiFi.setSleep(false);
    apMode=true;
  }
}

// ================= WLAN SCAN =================
void hScan(){
  int n=WiFi.scanNetworks();
  String s="<h2>WLAN Scan</h2>";
  for(int i=0;i<n;i++){
    s+=WiFi.SSID(i);
    s+=" (";
    s+=String(WiFi.RSSI(i));
    s+=" dBm) ";
    s+="<a href='/use?s="+WiFi.SSID(i)+"'>Use</a><br>";
  }
  s+="<br><a href='/'>Zurück</a>";
  server.send(200,"text/html",s);
}

void hUse(){
  wifiSSID=server.arg("s");
  server.send(200,"text/html",
    "<form action='/wifi'>"
    "SSID:<input name='s' value='"+wifiSSID+"'><br>"
    "PASS:<input name='w'><br>"
    "<input type='submit'></form>");
}

// ================= BACKUP =================
void hBackup(){
  String s="";
  s+="SSID="+wifiSSID+"\n";
  s+="PASS="+wifiPASS+"\n";
  s+="ADMIN="+adminPass+"\n";
  for(int i=1;i<=10;i++)
    s+="B"+String(i)+"="+String(bandToOut[i])+"\n";
  server.send(200,"text/plain",s);
}

// ================= RESTORE =================
void hRestore(){
  if(!server.hasArg("plain"))
    return server.send(400,"text/plain","No data");

  String data=server.arg("plain");

  while(data.length()){
    int pos=data.indexOf("\n");
    String line;
    if(pos==-1){ line=data; data=""; }
    else{ line=data.substring(0,pos); data=data.substring(pos+1); }

    if(line.startsWith("SSID=")) wifiSSID=line.substring(5);
    if(line.startsWith("PASS=")) wifiPASS=line.substring(5);
    if(line.startsWith("ADMIN=")) adminPass=line.substring(6);

    if(line.startsWith("B")){
      int b=line.substring(1,2).toInt();
      int o=line.substring(3).toInt();
      bandToOut[b]=o;
    }
  }

  saveConfig();
  server.send(200,"text/plain","Restored - rebooting");
  delay(1000);
  ESP.restart();
}

// ================= WEB =================
String page(){

  String s="<!DOCTYPE html><html><head>";
  s+="<title>BCD Antennaswitch</title>";
  s+="<meta name='viewport' content='width=device-width, initial-scale=1'>";
  s+="<style>";
  s+="body{font-family:sans-serif;background:#111;color:white;margin:0;padding:10px;}";
  s+=".card{background:#1c1c1c;padding:12px;border-radius:10px;margin-bottom:12px;}";
  s+=".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:8px;}";
  s+=".btn{padding:10px;border-radius:6px;background:#333;color:white;text-decoration:none;text-align:center;display:block;}";
  s+=".btn.active{background:#00aa00;font-weight:bold;}";
  s+="input,textarea{width:100%;padding:6px;margin-top:4px;margin-bottom:8px;border:none;border-radius:6px;}";
  s+="</style></head><body>";

  s+="<h2>Antenna Router</h2>";

  s+="<div class='card'>";
  if(currentBand<1)
    s+="Band: ---<br>Antenne: None!<br>";
  else
    s+="Band: "+bands[currentBand]+"<br>Antenne: "+String(currentOut+1)+"<br>";
  s+="</div>";

  s+="<div class='card'>";
  s+="<form method='get'>Passwort:<br>";
  s+="<input type='password' name='p'>";
  s+="<input type='submit' value='Login'></form>";
  s+="</div>";

  if(loggedIn()){

    s+="<div class='card'><h3>Band Mapping</h3>";
    for(int i=1;i<=10;i++){
      s+="<b>"+bands[i]+"</b>";
      s+="<div class='grid'>";
      for(int o=0;o<4;o++){
        s+="<a class='btn";
        if(bandToOut[i]==o) s+=" active";
        s+="' href='/set?b="+String(i)+"&o="+String(o)+"&p="+adminPass+"'>";
        s+="Ausgang "+String(o+1)+"</a>";
      }
      s+="</div><br>";
    }
    s+="</div>";

    s+="<div class='card'><h3>WLAN</h3>";
    s+="<a class='btn' href='/scan'>WLAN Scan</a><br><br>";
    s+="<form action='/wifi'>";
    s+="SSID:<input name='s'>";
    s+="PASS:<input name='w'>";
    s+="<input type='hidden' name='p' value='"+adminPass+"'>";
    s+="<input type='submit' value='Speichern'>";
    s+="</form></div>";

    s+="<div class='card'><h3>Netzwerk</h3>";
    s+="<form action='/net'>";
    s+="<input type='radio' name='mode' value='dhcp'";
    if(!useStaticIP) s+=" checked";
    s+="> DHCP<br>";
    s+="<input type='radio' name='mode' value='static'";
    if(useStaticIP) s+=" checked";
    s+="> Static<br><br>";

    s+="IP:<input name='ip' value='"+staticIP+"'>";
    s+="Gateway:<input name='gw' value='"+staticGW+"'>";
    s+="Subnet:<input name='mask' value='"+staticMask+"'>";
    s+="DNS:<input name='dns' value='"+staticDNS+"'>";

    s+="<input type='hidden' name='p' value='"+adminPass+"'>";
    s+="<input type='submit' value='Speichern'>";
    s+="</form></div>";

    s+="<div class='card'><h3>Backup</h3>";
    s+="<a class='btn' href='/backup'>Backup</a><br><br>";
    s+="<form method='post' action='/restore'>";
    s+="<textarea name='plain' rows=6></textarea>";
    s+="<input type='submit' value='Restore'>";
    s+="</form></div>";
  }

  s+="</body></html>";
  return s;
}

// ================= HANDLER =================
void hRoot(){ server.send(200,"text/html",page()); }

void hSet(){

  if(!loggedIn())
    return server.send(403,"text/plain","Login!");

  int b = server.arg("b").toInt();
  int o = server.arg("o").toInt();

  if(b>=1 && b<=10 && o>=0 && o<4){

    bandToOut[b]=o;
    saveConfig();

    if(currentBand==b){
      currentOut=o;
      switchOutput(o);
      updateDisplay();
    }
  }

  server.send(200,"text/html",page());
}

void hWifi(){
  if(!loggedIn()) return server.send(403,"text/plain","Login!");
  wifiSSID=server.arg("s");
  wifiPASS=server.arg("w");
  saveConfig();
  server.send(200,"text/plain","Saved - rebooting");
  delay(1000);
  ESP.restart();
}

void hNet(){
  if(!loggedIn()) return server.send(403,"text/plain","Login!");

  useStaticIP = (server.arg("mode")=="static");

  staticIP   = server.arg("ip");
  staticGW   = server.arg("gw");
  staticMask = server.arg("mask");
  staticDNS  = server.arg("dns");

  saveConfig();
  server.send(200,"text/plain","Saved - rebooting");
  delay(1000);
  ESP.restart();
}

// ================= SETUP =================
void setup(){

  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();

  pinMode(4, OUTPUT);
  digitalWrite(4, HIGH);

  for(int i=0;i<4;i++){
    pinMode(outputs[i],OUTPUT);
    pinMode(inputs[i],INPUT);
  }

  tft.init();
  tft.setRotation(1);
  drawStaticUI();
  updateDisplay();

  startWiFi();

  server.on("/",hRoot);
  server.on("/set",hSet);
  server.on("/wifi",hWifi);
  server.on("/backup",hBackup);
  server.on("/restore",HTTP_POST,hRestore);
  server.on("/scan",hScan);
  server.on("/use",hUse);
  server.on("/net",hNet);

  server.begin();
}

// ================= LOOP =================
void loop(){

  server.handleClient();

  static unsigned long lastCheck=0;
  if(millis()-lastCheck>150){
    lastCheck=millis();
    int band=readBand();
    applyRoute(band);
  }
}