//#define SIMULATION
//#define DEBUG_ESP_DNS

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <HardwareSerial.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <uptime_formatter.h>

#include "indexhtml.h"
#include "stylecss.h"

/************************************************************
 *
 * IP config definitions
 *
 ************************************************************/

// Variables to save values from HTML form
String ssid;
String password;
String ip;
String subnet;
String gateway;
String dns1;
String dns2;
String dhcp;
String mqttIp;
String mqttUser;
String mqttPassword;
String telnetPassword;
String rackCount;

// File paths to save input values permanently
const char *ssidPath = "/ssid.txt";
const char *passwordPath = "/password.txt";
const char *ipPath = "/ip.txt";
const char *subnetPath = "/subnet.txt";
const char *dns1Path = "/dns1.txt";
const char *dns2Path = "/dns2.txt";
const char *gatewayPath = "/gateway.txt";
const char *dhcpPath = "/dhcp.txt";
const char *mqttIpPath = "/mqttIp.txt";
const char *mqttUserPath = "/mqttUser.txt";
const char *mqttPasswordPath = "/mqttPassword.txt";
const char *telnetPasswordPath = "/telnetPassword.txt";
const char *rackCountPath = "/rackCount.txt";

IPAddress localIP;
IPAddress localSubnet;
IPAddress localDns1;
IPAddress localDns2;
IPAddress localGateway;

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000; // interval to wait for Wi-Fi connection (milliseconds)

#ifdef SIMULATION
#define MQTT_CLIENT_ID "pytes2"
#define MQTT_DISCOVER_PREFIX "pytes2_discover"
#else
#define MQTT_CLIENT_ID "pytes"
#define MQTT_DISCOVER_PREFIX "homeassistant/sensor"
#endif

/************************************************************
 *
 * RS232C definitions
 *
 ************************************************************/

IPAddress mqttServer(172, 16, 17, 42);

#define RXD2 16
#define TXD2 17

/************************************************************
 *
 * MQTT definitions
 *
 ************************************************************/

#define HEARDBEAT_TIMER 30000
#define MQTT_RETRY 500

typedef enum
{
  mqttString,
  mqttFloat,
  mqttInt,
  mqttBool
} VALUE_TYPE;

// types
typedef struct
{
  String name;
  String unit;
  VALUE_TYPE valueType;
  int precision;
  float factor;
  bool ignore;
} TABLE_COLUMN_DEFINITION;

// global objects
const TABLE_COLUMN_DEFINITION batCmd[] = {
    {"cell", "#", mqttInt, 0, 1.0, false},
    {"volt", "V", mqttFloat, 3, 0.001, false},
    {"current", "A", mqttFloat, 2, 0.001, false},
    {"temperature", "°C", mqttFloat, 1, 0.001, false},
    {"base_state", "", mqttString, 0, 1.0, false},
    {"voltage_state", "", mqttString, 0, 1.0, false},
    {"current_state", "", mqttString, 0, 1.0, false},
    {"temperature_state", "", mqttString, 0, 1.0, false},
    {"coulomb", "%", mqttInt, 0, 0.001, false},
    {"capacity", "AH", mqttFloat, 4, 0.0001, false},
    {"", "", mqttString, 0, 1.0, true}};

/************************************************************
 *
 * services definitions
 *
 ************************************************************/

WiFiClient WifiClient;
AsyncWebServer httpServer(80);
DNSServer dnsServer;
WiFiServer wifiServer;
PubSubClient mqttClient(mqttServer, 1883, WifiClient);



/************************************************************
 *
 * state var definitions
 *
 ************************************************************/

unsigned long keepalivetime = 0;
unsigned long MQTT_reconnect = 0;
unsigned long beat_time;
int heartBeatValue = 0;
int startDataCollection = 1;
int lineLogging = 0;
int commandLogging = 0;
int valueLogging = 0;
int discoveryLogging = 0;
int discoveryCounter = 3;
bool telnetLoginNeeded = false; 
bool captivePortalMode = false;
static std::vector<AsyncClient *> telnetClients;         // a list to hold all telnetClients
String telnetBuffer;


/************************************************************
 *
 * debug handling
 *
 ************************************************************/

void debug(const char *m)
{
  Serial.print(m);

  size_t count = strlen(m);
  for (auto &client : telnetClients)
  {
    if (client->space() > count && client->canSend())
    {
      int hasLineEnd = false;

      if(m[count-1] == '\n') {
        hasLineEnd = true;
        count--;
      }
      
      client->add(m, count);

      if(hasLineEnd) {
        client->add("\r\n", 2);
      }

      client->send();
    }
  }
}

void debug(String m)
{
  debug(m.c_str());
}

void debugLn(const char *m)
{
  debug(m);
  debug("\n");
}

void debugLn(String m)
{
  debugLn(m.c_str());
}

size_t debugPrintf(const char *format, ...)
{
  va_list arg;
  va_start(arg, format);
  char temp[64];
  char *buffer = temp;
  size_t len = vsnprintf(temp, sizeof(temp), format, arg);
  va_end(arg);
  if (len > sizeof(temp) - 1)
  {
    buffer = new (std::nothrow) char[len + 1];
    if (!buffer)
    {
      return 0;
    }
    va_start(arg, format);
    vsnprintf(buffer, len + 1, format, arg);
    va_end(arg);
  }

  debug(buffer);

  if (buffer != temp)
  {
    delete[] buffer;
  }
  return len;
}

/************************************************************
 *
 * Telnet types and vector helper handling
 *
 ************************************************************/

void erase(std::vector<AsyncClient *> &v, AsyncClient *key)
{
    for (auto it = v.begin(); it != v.end();)
    {
        AsyncClient *telnetClient = *it;
        if (telnetClient == key) {
            it = v.erase(it);
            delete telnetClient;
            return;
        }
        else {
            ++it;
        }
    }
}

/************************************************************
 *
 * SPIFF handling
 *
 ************************************************************/

void initSPIFFS()
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("An error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
}

String readFile(fs::FS &fs, const char *path)
{
  debugPrintf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    debugLn("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available())
  {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
  debugPrintf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    debugLn("- failed to open file for writing");
    return;
  }
  if (file.print(message) > 0)
  {
    debugLn("- file written");
  }
  else
  {
    debugLn("- fwrite failed");
  }
}

/************************************************************
 *
 * config
 *
 ************************************************************/

void readConfig()
{
  // Load values saved in SPIFFS
  dhcp = readFile(SPIFFS, dhcpPath);
  ssid = readFile(SPIFFS, ssidPath);
  password = readFile(SPIFFS, passwordPath);
  ip = readFile(SPIFFS, ipPath);
  subnet = readFile(SPIFFS, subnetPath);
  dns1 = readFile(SPIFFS, dns1Path);
  dns2 = readFile(SPIFFS, dns2Path);
  gateway = readFile(SPIFFS, gatewayPath);
  mqttIp = readFile(SPIFFS, mqttIpPath);
  mqttUser = readFile(SPIFFS, mqttUserPath);
  mqttPassword = readFile(SPIFFS, mqttPasswordPath);
  telnetPassword = readFile(SPIFFS, telnetPasswordPath);
  rackCount = readFile(SPIFFS, rackCountPath);

  Serial.printf("ssid          : '%s'\n", ssid.c_str());
  Serial.printf("password      : '%s'\n", password.c_str());
  Serial.printf("mqttUser      : '%s'\n", mqttUser.c_str());
  Serial.printf("mqttIp        : '%s'\n", mqttIp.c_str());
  Serial.printf("mqttPassword  : '%s'\n", mqttPassword.c_str());
  Serial.printf("telnetPassword: '%s'\n", telnetPassword.c_str());
  Serial.printf("rackCount     : '%s'\n", rackCount.c_str());
  Serial.printf("dhcp          : '%s'\n", dhcp.c_str());
  Serial.printf("ip            : '%s'\n", ip.c_str());
  Serial.printf("subnet        : '%s'\n", subnet.c_str());
  Serial.printf("dns1          : '%s'\n", dns1.c_str());
  Serial.printf("dns2          : '%s'\n", dns2.c_str());
  Serial.printf("gateway       : '%s'\n", gateway.c_str());

  if (telnetPassword.length() > 0) {
    telnetLoginNeeded = true;
  }
}


/************************************************************
 *
 * Console handling
 *
 ************************************************************/

int handleCommand(String line)
{
  // check commands
  if (line.startsWith(".discovery"))
  {
    discoveryCounter = 3;
    debugLn("discovery enabled");
    return 0;
  }
  else if (line.startsWith(".start"))
  {
    startDataCollection = 1;
    debugLn("data collection started");
    return 0;
  }
  else if (line.startsWith(".stop"))
  {
    startDataCollection = 0;
    debugLn("data collection stopped");
    return 0;
  }
  else if (line.startsWith(".restart"))
  {
    debugLn("restarting...");
    delay(1000);
    for (auto &client : telnetClients)
    {
      client->close();
    }
    delay(1000);
    ESP.restart();
    return 0;
  }
  else if (line.startsWith(".ll"))
  {
    lineLogging ^= 1;
    debugPrintf("line logging %s\n", (lineLogging ? "enabled" : "disabled"));
    return 0;
  }
  else if (line.startsWith(".lc"))
  {
    commandLogging ^= 1;
    debugPrintf("command logging %s\n", (commandLogging ? "enabled" : "disabled"));
    return 0;
  }
  else if (line.startsWith(".lv"))
  {
    valueLogging ^= 1;
    debugPrintf("value logging %s\n", (valueLogging ? "enabled" : "disabled"));
    return 0;
  }
  else if (line.startsWith(".ld"))
  {
    discoveryLogging ^= 1;
    debugPrintf("discovery logging %s\n", (discoveryLogging ? "enabled" : "disabled"));
    return 0;
  }
  else if (line.startsWith("."))
  {
    if (line.length() > 1)
    {
      debugPrintf("unknown command '%s'\n", line.c_str());
      debugLn("");
    }
    debugLn("known commands:");
    debugLn(".discover        restart discovery");
    debugLn(".restart         restart ESP32");
    debugLn(".stop            stop collecting data from PYTES");
    debugLn(".start           start collecting data from PYTES");
    debugLn(".ll              toogle line logging");
    debugLn(".lc              toogle command logging");
    debugLn(".lv              toogle mqtt value logging");
    debugLn(".ld              toogle mqtt discovery logging");
    return 0;
  }

  return 1;
}

/************************************************************
 *
 * HTTP
 *
 ************************************************************/

String httpStateProcessing(void)
{
  int i;
  String result = indexhtml;

  //result = template;

  if (dhcp.startsWith("on")) {
    result.replace("%DHCP%", "checked");
  } else {
    result.replace("%DHCP%", "");
  }

  result.replace("%SSID%", ssid);
  result.replace("%PASSWORD%", password);
  result.replace("%IP%", ip);
  result.replace("%SUBNET%", subnet);
  result.replace("%DNS1%", dns1);
  result.replace("%DNS2%", dns2);
  result.replace("%GATEWAY%", gateway);
  result.replace("%MQTTIP%", mqttIp);
  result.replace("%MQTTUSER%", mqttUser);
  result.replace("%MQTTPASSWORD%", mqttPassword);
  result.replace("%TELNETPASSWORD%", telnetPassword);
  result.replace("%RACKCOUNT%", rackCount);

  return result;
}


void handleHtppRequest(AsyncWebServerRequest *request)
{
  AsyncResponseStream *response;

  Serial.printf("HTML: %s%s\n", request->host().c_str(), request->url().c_str());

  String url = request->url();
  url.toLowerCase();

  if (url.endsWith(".css")) {
    response = request->beginResponseStream("text/css");
    response->print(stylecss);
  } else {
    response = request->beginResponseStream("text/html");
    response->print(httpStateProcessing());
  }

  request->send(response);
}

void handleConfigPost(AsyncWebServerRequest *request)
{
  Serial.println("a");
  int params = request->params();
  Serial.println(params);
  for (int i = 0; i < params; i++)
  {
    AsyncWebParameter *p = request->getParam(i);
    Serial.println(i);
    Serial.println(p->isPost());
    if (p->isPost())
    {
      // HTTP POST ssid value
      if (p->name() == "ssid")
      {
        ssid = p->value().c_str();
        Serial.print("SSID set to: ");
        Serial.println(ssid);
        // Write file to save value
        writeFile(SPIFFS, ssidPath, ssid.c_str());
      }
      // HTTP POST pass value
      if (p->name() == "password")
      {
        password = p->value().c_str();
        Serial.print("Password set to: ");
        Serial.println(password);
        // Write file to save value
        writeFile(SPIFFS, passwordPath, password.c_str());
      }
      // HTTP POST ip value
      if (p->name() == "ip")
      {
        ip = p->value().c_str();
        Serial.print("IP Address set to: ");
        Serial.println(ip);
        // Write file to save value
        writeFile(SPIFFS, ipPath, ip.c_str());
      }
      // HTTP POST subnet value
      if (p->name() == "subnet")
      {
        subnet = p->value().c_str();
        Serial.print("Subnet set to: ");
        Serial.println(subnet);
        // Write file to save value
        writeFile(SPIFFS, subnetPath, subnet.c_str());
      }
      // HTTP POST dns1 value
      if (p->name() == "dns1")
      {
        dns1 = p->value().c_str();
        Serial.print("DNS 1 set to: ");
        Serial.println(dns1);
        // Write file to save value
        writeFile(SPIFFS, dns1Path, dns1.c_str());
      }
      // HTTP POST dns1 value
      if (p->name() == "dns2")
      {
        dns2 = p->value().c_str();
        Serial.print("DNS 2 set to: ");
        Serial.println(dns2);
        // Write file to save value
        writeFile(SPIFFS, dns2Path, dns2.c_str());
      }
      // HTTP POST gateway value
      if (p->name() == "gateway")
      {
        gateway = p->value().c_str();
        Serial.print("Gateway set to: ");
        Serial.println(gateway);
        // Write file to save value
        writeFile(SPIFFS, gatewayPath, gateway.c_str());
      }
      // HTTP POST dhcp value
      if (p->name() == "dhcp")
      {
        dhcp = p->value().c_str();
        Serial.print("DHCP set to: ");
        Serial.println(dhcp);
        // Write file to save value
        writeFile(SPIFFS, dhcpPath, dhcp.c_str());
      }

      // HTTP POST mqttIp value
      if (p->name() == "mqttip")
      {
        mqttIp = p->value().c_str();
        Serial.print("MQTT IP set to: ");
        Serial.println(mqttIp);
        // Write file to save value
        writeFile(SPIFFS, mqttIpPath, mqttIp.c_str());
      }
      // HTTP POST mqttUser value
      if (p->name() == "mqttuser")
      {
        mqttUser = p->value().c_str();
        Serial.print("MQTT User set to: ");
        Serial.println(mqttUser);
        // Write file to save value
        writeFile(SPIFFS, mqttUserPath, mqttUser.c_str());
      }
      // HTTP POST mqttPassword value
      if (p->name() == "mqttpassword")
      {
        mqttPassword = p->value().c_str();
        Serial.print("MQTT Password set to: ");
        Serial.println(mqttPassword);
        // Write file to save value
        writeFile(SPIFFS, mqttPasswordPath, mqttPassword.c_str());
      }
      // HTTP POST telnetPassword value
      if (p->name() == "telnetpassword")
      {
        telnetPassword = p->value().c_str();
        Serial.print("Telnet Password set to: ");
        Serial.println(telnetPassword);
        // Write file to save value
        writeFile(SPIFFS, telnetPasswordPath, telnetPassword.c_str());
      }
      // HTTP POST rackCount value
      if (p->name() == "rackcount")
      {
        rackCount = p->value().c_str();
        Serial.print("Rack count set to: ");
        Serial.println(rackCount);
        // Write file to save value
        writeFile(SPIFFS, rackCountPath, rackCount.c_str());
      }

      // Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
    }
  }

  if (params > 0) {
    if (dhcp.startsWith("on"))
    {
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address given by DHCP");
    }
    else
    {
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
    }

    delay(3000);
    ESP.restart();
  }

  request->send(200, "text/plain", "nothing changed");
}

/************************************************************
 *
 * WIFI Stuff
 *
 ************************************************************/

bool initWiFi()
{
  if (ssid == "")
  {
    Serial.println("Undefined SSID.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  if (!dhcp.startsWith("on"))
  {
    if (ip == "")
    {
      Serial.println("Undefined SSID.");
      return false;
    }
    localIP.fromString(ip.c_str());
    localSubnet.fromString(subnet.c_str());
    localDns1.fromString(dns1.c_str());
    localDns2.fromString(dns2.c_str());
    localGateway.fromString(gateway.c_str());

    if (!WiFi.config(localIP, localGateway, localSubnet, localDns1, localDns2))
    {
      Serial.println("STA Failed to configure");
      return false;
    }
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED)
  {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  ip = WiFi.localIP().toString();
  subnet = WiFi.subnetMask().toString();
  dns1 = WiFi.dnsIP(0).toString();
  dns2 = WiFi.dnsIP(1).toString();
  gateway = WiFi.gatewayIP().toString();

  Serial.printf("ip     : '%s'\n", ip.c_str());
  Serial.printf("subnet : '%s'\n", subnet.c_str());
  Serial.printf("dns1   : '%s'\n", dns1.c_str());
  Serial.printf("dns2   : '%s'\n", dns2.c_str());
  Serial.printf("gateway: '%s'\n", gateway.c_str());

  return true;
}

void enableWiFi()
{
  if (!initWiFi())
  {
    // cleanup
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_AP);

    Serial.println("Setting AP (Access Point)");
    WiFi.softAP("PYTES-E-BOX-48100-R", NULL);

    // keep maybe 8.8.4.4 is needed for arduino devices
    //WiFi.softAP("PYTES-E-BOX-48100-R");
    //IPAddress accessPointIpAddress = IPAddress(192,168,4,1);
    //WiFi.softAPConfig(accessPointIpAddress, accessPointIpAddress, IPAddress(255, 255, 255, 0));

    IPAddress accessPointIpAddress = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(accessPointIpAddress);

    //dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", accessPointIpAddress);

    Serial.println("Starting own access point in captive portal mode");
    captivePortalMode = true;
  }

  // http server routes
  httpServer.onNotFound(handleHtppRequest);
  httpServer.on("/", HTTP_POST, handleConfigPost);
  httpServer.begin();

  ArduinoOTA.begin();
}

/************************************************************
 *
 * MQTT Stuff
 *
 ************************************************************/
void reconnect()
{
  // Loop until we're reconnected
  while (!mqttClient.connected())
  {
    debugLn("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect(MQTT_CLIENT_ID, "iot", "iotiot"))
    {
      debugLn("MQTT connected");
      // Once connected, publish an announcement...
      mqttClient.publish(MQTT_CLIENT_ID "/heartBeat", "beat");
      // ... and resubscribe
      mqttClient.subscribe(MQTT_CLIENT_ID "/reset");
      // Can subscribe to Out relay Aux Here
    }
    else
    {
      debugPrintf("failed, rc=%d try again in 5 seconds\n", mqttClient.state());
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/************************************************************
 *
 * Telnet stuff
 *
 ************************************************************/
void debugPrintTelnetProtocolLine(String direction, String line) {
  Serial.print(line.length());
  Serial.print(" " + direction + " ");

  for(int i=0; i<3; i++) {
    Serial.printf("%02.2X ", line[i]);
  }

  if (line.length()>=2) {
    uint8_t c = line[1];
    switch(c) 
    {
    case 0xfb:
      Serial.print("Will ");
      break;
    case 0xfc:
      Serial.print("WON'T ");
      break;
    case 0xfd:
      Serial.print("DO ");
      break;
    case 0xfe:
      Serial.print("DON'T ");
      break;
    case 0xff:
      Serial.print("CMD ");
      break;
    }
  }
  if (line.length()>=3) {
    uint8_t c = line[2];
    switch(c) 
    {
    case 0:
      Serial.print("TRANSMIT-BINARY");
      break;
    case 1:
      Serial.print("ECHO");
      break;
    case 3:
      Serial.print("SUPPRESS-GO-AHEAD");
      break;
    case 5:
      Serial.print("STATUS");
      break;
    case 6:
      Serial.print("TIMING-MARK");
      break;
    case 10:
      Serial.print("NAOCRD");
      break;
    case 11:
      Serial.print("NAOHTS");
      break;
    case 12:
      Serial.print("NAOHTD");
      break;
    case 13:
      Serial.print("NAOFFD");
      break;
    case 14:
      Serial.print("NAOVTS");
      break;
    case 15:
      Serial.print("NAOVTD");
      break;
    case 16:
      Serial.print("NAOLFD");
      break;
    case 17:
      Serial.print("EXTEND-ASCII");
      break;
    case 24:
      Serial.print("TERMINAL-TYPE");
      break;
    case 31:
      Serial.print("NAWS");
      break;
    case 32:
      Serial.print("TERMINAL-SPEED");
      break;
    case 33:
      Serial.print("TOGGLE-FLOW-CONTROL");
      break;
    case 34:
      Serial.print("LINEMODE");
      break;
    case 37:
      Serial.print("AUTHENTICATION");
      break;
    }
  }
  Serial.println("");;
}

void handleTelnetProtocol(AsyncClient *client, String line) {
  //debugPrintf("telnet protocol from client %s %d [%02.2X %02.2X %02.2X]\n", client->remoteIP().toString().c_str(), line.length(), line[0], line[1], line[3]);
  debugPrintTelnetProtocolLine("R", line);
}

void handleTelnetLine(AsyncClient *client, String line)
{
  //debugPrintf("line from client %s '%s'\n", client->remoteIP().toString().c_str(), line.c_str());

  // password handling
  if (telnetLoginNeeded) {
    if (line == telnetPassword) {
      telnetLoginNeeded = false;
      //debugPrintTelnetProtocolLine(String("S"),String("\xFF\xFD\x01"));
      //client->write("\xFF\xFD\x01\r\n");
      client->write("access granted\r\n");
    } else {
      //debugPrintTelnetProtocolLine(String("S"),String("\xFF\xFD\x01"));
      //client->write("\xFF\xFE\x01\r\n");
      client->write("\nenter password: ");
    }
    return;
  }

  // command handling
  if (handleCommand(line))
  {
    Serial2.write(line.c_str());
    Serial2.write("\r\n");
    Serial2.flush();
  }
}

static void handleTelnetError(void *arg, AsyncClient *client, int8_t error)
{
  debugPrintf("connection error %s from client %s\n", client->errorToString(error), client->remoteIP().toString().c_str());
}


static void handleTelnetData(void *arg, AsyncClient *client, void *data, size_t len)
{
  //debugPrintf("data received from client %s %d (%p,%p)\n", client->remoteIP().toString().c_str(), len, arg, client);

  uint8_t *input = (uint8_t *) data;
  for (int i = 0; i < len; i++)
  {
    telnetBuffer += char(input[i]&0xff);
  }

  int lineFeedIndex = telnetBuffer.indexOf("\n");
  while(lineFeedIndex>0) {
    String line = telnetBuffer.substring(0,lineFeedIndex);
    line.replace("\r","");

    while (line.startsWith("\xFF")) {
      handleTelnetProtocol(client, line);
      line = line.substring(3);
    } 
    
    handleTelnetLine(client, line);

    telnetBuffer = telnetBuffer.substring(lineFeedIndex+1);
    lineFeedIndex = telnetBuffer.indexOf("\n")+1;
  }
}

static void handleTelnetDisconnect(void *arg, AsyncClient *client)
{
  debugPrintf("client %s disconnected\n", client->remoteIP().toString().c_str());
  erase(telnetClients, client);
  if (telnetPassword.length() > 0) {
    telnetLoginNeeded = true;
  }
}

static void handleTelnetTimeOut(void *arg, AsyncClient *client, uint32_t time)
{
  debugPrintf("client ACK timeout ip: %s\n", client->remoteIP().toString().c_str());
  erase(telnetClients, client);  
  if (telnetPassword.length() > 0) {
    telnetLoginNeeded = true;
  }
}

/* server events */
static void handleNewTelnetClient(void *arg, AsyncClient *client)
{
  debugPrintf("new client has been connected to server, ip: %s (%p,%p)\n", client->remoteIP().toString().c_str(), arg, client);

  // add to list
  telnetClients.push_back(client);

  // register events
  client->onData(&handleTelnetData, NULL);
  client->onError(&handleTelnetError, NULL);
  client->onDisconnect(&handleTelnetDisconnect, NULL);
  client->onTimeout(&handleTelnetTimeOut, NULL);

  client->write("\r\nHello World from PYTES 48100R\r\n");

  String uptime = "up ";
  uptime += uptime_formatter::getUptime();
  uptime += "\r\n";
  client->write(uptime.c_str());

  client->write("press '.' for help\r\n");

  if (telnetLoginNeeded) {
      //debugPrintTelnetProtocolLine(String("S"),String("\xFF\xFE\x01"));
      //client->write("\xFF\xFE\x01\r\n");
      client->write("\nenter password: ");
    }  
}

void telnet()
{
  int count = Serial2.available();
  if (count > 0)
  {
    char data[count];
    Serial2.readBytes(data, count);
    for (auto &client : telnetClients)
    {
      if (client->space() > count && client->canSend())
      {
        client->add(data, count);
        client->send();
      }
    }
  }
}

/************************************************************
 *
 * RS232C definitions
 *
 ************************************************************/

void clearSerialBuffer()
{
  Serial2.println("");
  Serial2.flush();
  delay(10);
  while (true)
  {
    int count = Serial2.available();
    if (count > 0)
    {
      char data[count];
      Serial2.readBytes(data, count);
      if (lineLogging)
      {
        String d = "";
        for (int i = 0; i < count; i++)
        {
          d += char(data[i]);
        }
        d.replace("\r", "");
        d.replace("\n", "");
        debugPrintf("clear: '%s'\n", d.c_str());
      }
      delay(10);
    }
    else
    {
      break;
    }
  }
}

int serialReacts()
{
  int i;

  for (i = 0; i < 100; i++)
  {
    delay(10);
    if (Serial2.available())
    {
      if (commandLogging)
        debugLn("info : PYTES is reacting");
      return 1;
    }
  }

  if (commandLogging)
    debugLn("info : no reaction from PYTES");
  return 0;
}

String serialReadLine()
{
  String buffer;
  int maxRetrys = 100;
  while (maxRetrys >= 0)
  {
    buffer = "";
    buffer = Serial2.readStringUntil('\n');
    if (buffer.length() == 0)
    {
      maxRetrys--;
      delay(10);
      continue;
    }

    buffer.replace("\r", "");
    buffer.replace("\n", "");

    // skip empty lines
    if (buffer.length() == 0)
    {
      maxRetrys = 100;
      continue;
    }

    if (buffer[0] == 0)
    {
      maxRetrys = 100;
      continue;
    }

    break;
  }

  if (lineLogging)
    debugPrintf("line : '%s'\n", buffer.c_str());
  return buffer;
}

void serialCommand(String cmd)
{
  clearSerialBuffer();
  delay(100);

  if (commandLogging)
    debug("cmd  : '");
  // send cmd in single chars and slowly
  for (int i = 0; i < cmd.length(); i++)
  {
    if (commandLogging)
      debugPrintf("%c", cmd[i]);
    Serial2.write(cmd[i]);
    Serial2.flush();
    #ifndef SIMULATION
      delay(250);
    #endif
  }
  if (commandLogging)
    debug("'\n");
  Serial2.write('\n');
  Serial2.flush();
}

void sendCommandAndParseForColon(String cmd, String subTopic1, String subTopic2)
{
  String buffer = "";

  serialCommand(cmd);
  if (!serialReacts())
  {
    return;
  }

  // wait for @
  while (true)
  {
    buffer = serialReadLine();
    if (buffer[0] == 0)
    {
      return;
    }
    if (buffer[0] == '@')
    {
      break;
    }
  }

  // wait for \n
  buffer = serialReadLine();
  if (buffer[0] == 0)
  {
    return;
  }

  int i = 0;
  uint8_t c;
  while (true)
  {
    buffer = serialReadLine();

    if (buffer.length() <= 0 || buffer[0] == '$')
    {
      break;
    }

    int seperatorPos = buffer.indexOf(':');
    if (seperatorPos > 0)
    {
      String unit = "";
      String name = buffer.substring(0, seperatorPos - 1);
      name.toLowerCase();
      name.trim();
      name.replace("  ", " ");
      name.replace(" ", "_");
      name.replace("tmpr.", "temperature");
      name.replace("coul.", "coulomb");
      name.replace("soh.", "state_of_health");
      name.replace("sec.", "seconds");

      String value = buffer.substring(seperatorPos + 1);
      value.trim();
      while (value.indexOf("  ") >= 0)
      {
        value.replace("  ", " ");
      }
      seperatorPos = value.indexOf(' ');
      if (seperatorPos > 0)
      {
        unit = value.substring(seperatorPos);
        unit.trim();

        if (unit.length() >= 10)
        {
          unit = "";
        }
        else
        {
          value = value.substring(0, seperatorPos);
        }
      }

      String jsonValue = "";
      jsonValue += "{";
      jsonValue += "\"value\": ";

      if (unit.startsWith("mV") || unit.startsWith("mAH") || unit.startsWith("mA") || unit.startsWith("mC"))
      {
        char buf[10];
        char fmt[10];

        unit = unit.substring(1);

        double f = atof(value.c_str());
        f *= 0.001;

        strcpy(fmt, "%. f");
        if (unit.startsWith("mc"))
        {
          fmt[2] = '1';
        }
        else
        {
          fmt[2] = '3';
        }
        sprintf(buf, fmt, f);

        jsonValue += buf;
        jsonValue += ",";
      }
      else if (unit.startsWith("%") || unit.startsWith("s"))
      {
        if (unit.startsWith("s"))
        {
          unit = "S";
        }
        int i = atoi(value.c_str());
        jsonValue += i;
        jsonValue += ",";
      }
      else
      {
        jsonValue += "\"";
        jsonValue += value;
        jsonValue += "\",";
      }

      jsonValue += "\"unit\": ";
      jsonValue += "\"";
      jsonValue += unit;
      jsonValue += "\"";

      jsonValue += "}";

      // buid topic
      String topic = MQTT_CLIENT_ID;
      if (subTopic1.length() > 0)
      {
        topic += "/";
        topic += subTopic1;
      }
      if (subTopic2.length() > 0)
      {
        topic += "/";
        topic += subTopic2;
      }
      topic += "/";
      topic += name;

      // log
      if (valueLogging)
        debugPrintf("mqtt : '%s' = '%s'\n", topic.c_str(), jsonValue.c_str());

      // send to mqtt
      mqttClient.publish(topic.c_str(), jsonValue.c_str(), true);

      if (discoveryCounter > 0)
      {
        // autodiscover
        String autodiscoverTopic = MQTT_DISCOVER_PREFIX;
        autodiscoverTopic += "/";
        autodiscoverTopic += MQTT_CLIENT_ID;
        if (subTopic1.length() > 0 || subTopic2.length() > 0)
        {
          autodiscoverTopic += "_";
        }
        if (subTopic1.length() > 0)
        {
          autodiscoverTopic += subTopic1;
        }
        if (subTopic1.length() > 0 && subTopic2.length() > 0)
        {
          autodiscoverTopic += "_";
        }
        if (subTopic2.length() > 0)
        {
          autodiscoverTopic += subTopic2;
        }
        autodiscoverTopic += "/";
        autodiscoverTopic += name;
        autodiscoverTopic += "/";
        autodiscoverTopic += "config";

        String discoverJson = "";
        discoverJson += "{";

        discoverJson += "\"name\": \"";
        discoverJson += MQTT_CLIENT_ID;
        if (subTopic1.length() > 0 || subTopic2.length() > 0)
        {
          discoverJson += "_";
        }
        if (subTopic1.length() > 0)
        {
          discoverJson += subTopic1;
        }
        if (subTopic1.length() > 0 && subTopic2.length() > 0)
        {
          discoverJson += "_";
        }
        if (subTopic2.length() > 0)
        {
          discoverJson += subTopic2;
        }
        discoverJson += "_";
        discoverJson += name;
        discoverJson += "\",";

        discoverJson += "\"unique_id\": \"";
        discoverJson += topic;
        discoverJson += "\",";

        discoverJson += "\"state_topic\": \"";
        discoverJson += topic;
        discoverJson += "\",";

        if (unit == "V")
        {
          discoverJson += "\"device_class\": \"voltage\",";
          discoverJson += "\"state_class\": \"measurement\",";
        }
        else if (unit == "A")
        {
          discoverJson += "\"device_class\": \"current\",";
          discoverJson += "\"state_class\": \"measurement\",";
        }
        else if (unit == "C")
        {
          discoverJson += "\"device_class\": \"temperature\",";
          discoverJson += "\"state_class\": \"measurement\",";
        }
        else if (unit == "%")
        {
          discoverJson += "\"device_class\": \"battery\",";
          discoverJson += "\"state_class\": \"measurement\",";
        }

        discoverJson += "\"qos\": 0,";

        discoverJson += "\"unit_of_measurement\": \"";
        discoverJson += unit;
        discoverJson += "\",";

        discoverJson += "\"value_template\": \"{{ value_json.value }}\",";
        discoverJson += "\"availability_mode\": \"all\",";
        discoverJson += "\"device\": {";

        discoverJson += "\"identifiers\": [\"";
        discoverJson += topic;
        discoverJson += "\"],";

        discoverJson += "\"name\": \"";
        discoverJson += MQTT_CLIENT_ID;
        if (subTopic1.length() > 0 || subTopic2.length() > 0)
        {
          discoverJson += " ";
        }
        if (subTopic1.length() > 0)
        {
          discoverJson += subTopic1;
        }
        if (subTopic2.length() > 0)
        {
          discoverJson += subTopic2;
        }
        discoverJson += " (";
        discoverJson += name;
        discoverJson += ")\",";

        discoverJson += "\"sw_version\": \"\"";

        discoverJson += "}";
        discoverJson += "}";

        // log
        if (discoveryLogging)
          debugPrintf("mqttd: '%s' = '%s'\n", autodiscoverTopic.c_str(), discoverJson.c_str());

        // send to mqtt
        mqttClient.publish(autodiscoverTopic.c_str(), discoverJson.c_str(), true);
      }
    }
  }
  clearSerialBuffer();
}

void sendCommandAndParseTable(const TABLE_COLUMN_DEFINITION valueDefinition[], String cmd, String subTopic1, String subTopic2)
{
  String buffer = "";

  serialCommand(cmd);
  if (!serialReacts())
  {
    return;
  }

  // wait for @
  while (true)
  {
    buffer = serialReadLine();
    if (buffer[0] == 0)
    {
      return;
    }
    if (buffer[0] == '@')
    {
      break;
    }
  }

  // wait for \n
  buffer = serialReadLine();
  if (buffer[0] == 0)
  {
    return;
  }

  int i = 0;
  uint8_t c;
  while (true)
  {
    buffer = serialReadLine();
    // debugPrintf("a) '%s'\n", buffer.c_str());
    buffer.trim();
    // debugPrintf("b) '%s'\n", buffer.c_str());

    if (buffer.length() <= 0 || buffer[0] == '$')
    {
      break;
    }

    if (isdigit(buffer[0]))
    {
      int startPos = 0;
      int columnIndex = 0;

      buffer.trim();
      while (buffer.indexOf("  ") >= 0)
      {
        buffer.replace("  ", " ");
      }

      do
      {
        if (!valueDefinition[columnIndex].ignore)
        {
          int endPos = buffer.indexOf(' ', startPos);
          if (endPos < 0)
          {
            endPos = buffer.length() - 1;
          }

          String value = buffer.substring(startPos, endPos);
          value.trim();
          startPos = endPos + 1;

          // buid topic
          String topic = MQTT_CLIENT_ID;
          if (subTopic1.length() > 0)
          {
            topic += "/";
            topic += subTopic1;
          }
          if (subTopic2.length() > 0)
          {
            topic += "/";
            topic += subTopic2;
          }
          topic += "/";
          topic += i + 1;
          topic += "/";
          topic += valueDefinition[columnIndex].name;

          String jsonValue = "";
          jsonValue += "{";
          jsonValue += "\"value\": ";

          switch (valueDefinition[columnIndex].valueType)
          {
          case mqttString:
          {
            jsonValue += "\"";
            jsonValue += value;
            jsonValue += "\",";
            break;
          }
          case mqttFloat:
          {
            char buf[10];
            char fmt[10];

            double f = atof(value.c_str());
            f *= valueDefinition[columnIndex].factor;

            strcpy(fmt, "%. f");
            fmt[2] = '0' + valueDefinition[columnIndex].precision;
            sprintf(buf, fmt, f + (.5 * valueDefinition[columnIndex].factor));

            jsonValue += buf;
            jsonValue += ",";
            break;
          }
          case mqttInt:
          {
            int i = atoi(value.c_str());
            jsonValue += i;
            jsonValue += ",";
            break;
          }
          case mqttBool:
          {
            jsonValue += value;
            jsonValue += ",";
            break;
          }
          }

          jsonValue += "\"unit\": ";
          jsonValue += "\"";
          jsonValue += valueDefinition[columnIndex].unit;
          jsonValue += "\"";

          jsonValue += "}";

          // log
          if (valueLogging)
            debugPrintf("mqtt : '%s' = '%s'\n", topic.c_str(), jsonValue.c_str());

          // send to mqtt
          mqttClient.publish(topic.c_str(), jsonValue.c_str(), true);

          if (discoveryCounter > 0)
          {
            // autodiscover
            String autodiscoverTopic = MQTT_DISCOVER_PREFIX;
            autodiscoverTopic += "/";
            autodiscoverTopic += MQTT_CLIENT_ID;
            if (subTopic1.length() > 0 || subTopic2.length() > 0)
            {
              autodiscoverTopic += "_";
            }
            if (subTopic1.length() > 0)
            {
              autodiscoverTopic += subTopic1;
            }
            if (subTopic1.length() > 0 && subTopic2.length() > 0)
            {
              autodiscoverTopic += "_";
            }
            if (subTopic2.length() > 0)
            {
              autodiscoverTopic += subTopic2;
            }
            autodiscoverTopic += "_";
            autodiscoverTopic += i + 1;
            autodiscoverTopic += "/";
            autodiscoverTopic += valueDefinition[columnIndex].name;
            ;
            autodiscoverTopic += "/";
            autodiscoverTopic += "config";

            String discoverJson = "";
            discoverJson += "{";

            discoverJson += "\"name\": \"";
            discoverJson += MQTT_CLIENT_ID;
            if (subTopic1.length() > 0 || subTopic2.length() > 0)
            {
              discoverJson += "_";
            }
            if (subTopic1.length() > 0)
            {
              discoverJson += subTopic1;
            }
            if (subTopic1.length() > 0 && subTopic2.length() > 0)
            {
              discoverJson += "_";
            }
            if (subTopic2.length() > 0)
            {
              discoverJson += subTopic2;
            }
            discoverJson += "_";
            discoverJson += i + 1;
            discoverJson += "_";
            discoverJson += valueDefinition[columnIndex].name;
            discoverJson += "\",";

            discoverJson += "\"unique_id\": \"";
            discoverJson += topic;
            discoverJson += "\",";

            discoverJson += "\"state_topic\": \"";
            discoverJson += topic;
            discoverJson += "\",";

            if (valueDefinition[columnIndex].unit == "V")
            {
              discoverJson += "\"device_class\": \"voltage\",";
              discoverJson += "\"state_class\": \"measurement\",";
            }
            else if (valueDefinition[columnIndex].unit == "A")
            {
              discoverJson += "\"device_class\": \"current\",";
              discoverJson += "\"state_class\": \"measurement\",";
            }
            else if (valueDefinition[columnIndex].unit == "C")
            {
              discoverJson += "\"device_class\": \"temperature\",";
              discoverJson += "\"state_class\": \"measurement\",";
            }
            else if (valueDefinition[columnIndex].unit == "%")
            {
              discoverJson += "\"device_class\": \"battery\",";
              discoverJson += "\"state_class\": \"measurement\",";
            }

            discoverJson += "\"qos\": 0,";

            discoverJson += "\"unit_of_measurement\": \"";
            discoverJson += valueDefinition[columnIndex].unit;
            discoverJson += "\",";

            discoverJson += "\"value_template\": \"{{ value_json.value }}\",";
            discoverJson += "\"availability_mode\": \"all\",";
            discoverJson += "\"device\": {";

            discoverJson += "\"identifiers\": [\"";
            discoverJson += topic;
            discoverJson += "\"],";

            discoverJson += "\"name\": \"";
            discoverJson += MQTT_CLIENT_ID;
            if (subTopic1.length() > 0 || subTopic2.length() > 0)
            {
              discoverJson += " ";
            }
            if (subTopic1.length() > 0)
            {
              discoverJson += subTopic1;
            }
            if (subTopic2.length() > 0)
            {
              discoverJson += subTopic2;
            }
            discoverJson += " ";
            discoverJson += i + 1;
            discoverJson += " (";
            discoverJson += valueDefinition[columnIndex].name;
            discoverJson += ")\",";

            discoverJson += "\"sw_version\": \"\"";
            discoverJson += "}";
            discoverJson += "}";

            // log
            if (discoveryLogging)
            {
              debugPrintf("mqttd: '%s' = '%s'\n", autodiscoverTopic.c_str(), discoverJson.c_str());
            }

            // send to mqtt
            mqttClient.publish(autodiscoverTopic.c_str(), discoverJson.c_str(), true);
          }
        }
        // next Column
        columnIndex++;
      } while (valueDefinition[columnIndex].name.length() > 0);
    }
    i++;
    continue;
  }
  clearSerialBuffer();
}

void heardBeat()
{
  // heart beat
  unsigned long time_passed = 0;
  time_passed = millis() - beat_time;
  if (time_passed < 0)
  {
    beat_time = millis();
  }

  if (time_passed > HEARDBEAT_TIMER)
  {
    time_passed = 0;
    heartBeatValue ^= 1;
    mqttClient.publish(MQTT_CLIENT_ID "/heartBeat", heartBeatValue == 1 ? "1" : "0");
    beat_time = millis();

    // send to MQTT
    if (startDataCollection)
    {
      clearSerialBuffer();
#ifdef SIMULATION
      sendCommandAndParseForColon("info 1\n@\nDevice address      : 1\nManufacturer        : PYTES\nDevice name         : E-BOX-48100R\nBoard version       : SQBMSV13\nMain Soft version   : SPBMS16SRP2111V1.3.6\nSoft  version       : V1.3\nBoot  version       : V1.6\nComm version        : V2.0\nRelease Date        : 22-06-06\nBarcode             : LC0B010402230255\nSpecification       : 51.2V/100AH\nCell Number         : 16\nMax Dischg Curr     : -102000mA\nMax Charge Curr     : 102000mA\nConsole Port rate   : 115200\nCommand completed successfully\n$$\nPYTES>", "info", "1");
      sendCommandAndParseTable(batCmd, "bat 1\n@\nBattery  Volt     Curr     Tempr    Base State   Volt. State  Curr. State  Temp. State  Coulomb\n0        3284     2146     19000    Charge       Normal       Normal       Normal        27%      27105 mAH\n1        3286     2146     19000    Charge       Normal       Normal       Normal        27%      27106 mAH\n2        3282     2146     19000    Charge       Normal       Normal       Normal        27%      27107 mAH\n3        3282     2146     19000    Charge       Normal       Normal       Normal        27%      27108 mAH\n4        3283     2146     19000    Charge       Normal       Normal       Normal        27%      27109 mAH\n5        3283     2146     19000    Charge       Normal       Normal       Normal        27%      27110 mAH\n6        3284     2146     19000    Charge       Normal       Normal       Normal        27%      27111 mAH\n7        3280     2146     19000    Charge       Normal       Normal       Normal        27%      27112 mAH\n8        3284     2146     19000    Charge       Normal       Normal       Normal        27%      27113 mAH\n9        3283     2146     19000    Charge       Normal       Normal       Normal        27%      27114 mAH\n10       3285     2146     18000    Charge       Normal       Normal       Normal        27%      27115 mAH\n11       3285     2146     18000    Charge       Normal       Normal       Normal        27%      27116 mAH\n12       3285     2146     18000    Charge       Normal       Normal       Normal        27%      27117 mAH\n13       3285     2146     18000    Charge       Normal       Normal       Normal        27%      27118 mAH\n14       3285     2146     18000    Charge       Normal       Normal       Normal        27%      27119 mAH\n15       3286     2146     18000    Charge       Normal       Normal       Normal        27%      27120 mAH\nCommand completed successfully\n$$\nPYTES>", "bat", "1");
      sendCommandAndParseForColon("pwr 1\n@\n ----------------------------\n Power  1\n Voltage         : 55012       mV\n Current         : 13181       mA\n Temperature     : 24000       mC\n Coulomb         : 97          %\n Total Coulomb   : 100000      mAH\n Real Coulomb    : 99900       mAH\n Max Voltage     : 58000       mV\n Charge Times    : 0\n Basic Status    : Charge\n Charge Sec.     : 0       s\n Volt Status     : Normal\n Current Status  : Normal\n Tmpr. Status    : Normal\n Coul. Status    : Normal\n Soh. Status     : Normal\n Heater Status   : OFF\n Protect ENA     : BOV BHV BLV BUV POV PHV PLV PUV CBOT CBHT CBLT CBUT DBOT DBHT DBLT DBUT POT PHT PLT PUT COC COC2 COCA DOCA DOC DOC2 SC LCOUL\n Bat Events      : 0x0\n Power Events    : 0x0\n System Fault    : 0x0\n ----------------------------\nCommand completed successfully\n$$\nPYTES>", "pwr", "1");
#else
      int maxCount = rackCount.toInt();
      for (int count = 1; count <= maxCount; count++)
      {
        String c = "";
        c += count;
        sendCommandAndParseForColon("info " + c, "info", c);
        sendCommandAndParseTable(batCmd, "bat " + c, "bat", c);
        sendCommandAndParseForColon("pwr " + c, "pwr", c);
      }

      sendCommandAndParseForColon("login debug", "", 0);
      sendCommandAndParseForColon("pwrsys", "pwrsys", 0);
      sendCommandAndParseForColon("logout", "", 0);

#endif
      clearSerialBuffer();
      if (discoveryCounter > 0)
      {
        discoveryCounter--;
      }
    }
  }
}


/************************************************************
 *
 * system setup
 *
 ************************************************************/

void setup()
{
  Serial.begin(115200);
  initSPIFFS();
  readConfig();
  enableWiFi();

  if (!captivePortalMode) {
    Serial2.setRxBufferSize(4096);
    Serial2.setTxBufferSize(4096);
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

    mqttClient.setBufferSize(1024);

    // open telnet port
    AsyncServer *telnetServer = new AsyncServer(23); // start listening on tcp port 23
    telnetServer->onClient(&handleNewTelnetClient, telnetServer);
    telnetServer->begin();
  }
}

/************************************************************
 *
 * let's rock
 *
 ************************************************************/

void loop()
{
  // captive portal
  if (captivePortalMode) {
    dnsServer.processNextRequest();
  } else {
    // Check for over the air update request and (if present) flash it
    ArduinoOTA.handle();

    // do mqtt
    mqttClient.loop();
    reconnect();

    // heart beat
    heardBeat();

    // provide intercative telnet server
    telnet();
  }



}
