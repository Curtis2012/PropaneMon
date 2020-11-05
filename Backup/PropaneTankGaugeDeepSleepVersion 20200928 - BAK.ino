/*

   2020-06-07 C. Collins created. Derived code from several sources including SparkFuns HX711 breakout example code
   2020-09-26 C. Collins ported to AdaFruit Huzzah Feather ESP8266 due to sleep problems with cheap NodeMCU's


*/

#include "HX711.h"
#include "cscNetServices.h"

#define DOUT  14
#define CLK   12
#define DEEPSLEEPTIME 300e6   // deep sleep time in ms
#define kgToLbsFactor 2.20462F  // dynamic calibration would be good, but...just use a constant for now.

HX711 scale;

struct propaneTank_t
{
  char tankType = 'P';
  int tankNum = 4;
  float capacityKg = 11.33;  // 25lbs = 11.33kg
  float tareKg = 6.486;       // 14.3lbs = 6.486kg
  float netPropaneWt = 0;
  float scaleWeight = 0;
  float percentFull = 0;
  int loAlarm = 0;
  unsigned long sampleInterval = 180000;
};

propaneTank_t propaneTank25lb;

float empericalScaleFactor = -23680.00;  // derived by testing with SparkFuns calibrate sketch

StaticJsonDocument<128> sampleDoc;

float propaneWeight(float grossTankWeight)
{

  propaneTank25lb.netPropaneWt = propaneTank25lb.scaleWeight - propaneTank25lb.tareKg;
  if (propaneTank25lb.netPropaneWt < 0) propaneTank25lb.netPropaneWt = 0;

  return (propaneTank25lb.netPropaneWt);
}

void handleMQTTmsg(char* topic, byte* payload, unsigned int length)
{
  Serial.println("MQTT message received");
}

void checkTank()
{
  scale.power_up();
  delay(1000);         // delay after power up
  scale.wait_ready_retry(3, 1000);
  scale.get_units(5);  // get a few readings and discard
  Serial.print("\nPropane tank: ");
  propaneTank25lb.scaleWeight = (scale.get_units(5) + 18.9);  // additional offset to compensate for not tare'ing scale first...factor into constant later...
  if (propaneTank25lb.scaleWeight < 0) propaneTank25lb.scaleWeight = 0;
  Serial.print(propaneTank25lb.scaleWeight, 2);
  Serial.print(" kg (");
  Serial.print((propaneTank25lb.scaleWeight * kgToLbsFactor), 2);
  Serial.print(" lbs)");
  propaneTank25lb.netPropaneWt = propaneWeight(propaneTank25lb.scaleWeight);
  Serial.print("  Net propane weight = ");
  Serial.print(propaneTank25lb.netPropaneWt, 2);
  Serial.print(" kg (");
  Serial.print((propaneTank25lb.netPropaneWt * kgToLbsFactor), 2);
  Serial.print(" lbs)");
  Serial.print("Percentage full: ");
  propaneTank25lb.percentFull = ((propaneTank25lb.netPropaneWt / propaneTank25lb.capacityKg) * 100);
  if (propaneTank25lb.percentFull < 0) propaneTank25lb.percentFull = 0;
  if (propaneTank25lb.percentFull > 100) propaneTank25lb.percentFull = 100;
  Serial.print(propaneTank25lb.percentFull, 1);
  Serial.println("%");
  scale.power_down();
}

void sendSample()
{
  bool rc = false;

  if (!mqttClient.connected()) {
    connectMQTT(false, MDNS.IP(hostEntry));
    delay(3000);
  };

  checkTank();
  sampleDoc["tType"] = propaneTank25lb.tankType;
  sampleDoc["t"] = propaneTank25lb.tankNum;
  sampleDoc["pF"] = (round(propaneTank25lb.percentFull * 100) / 100);
  serializeJson(sampleDoc, msgbuff);
  rc = mqttClient.publish(mqttTopic, msgbuff);
  if (!rc) Serial.print("MQTT publish failed!");

  if (debug)
  {
    Serial.print("\nMQTT msg published: ");
    Serial.println(msgbuff);
  }
}

void setup()
{
  debug = true;

  delay(5000); // allow for intervention
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(9600);
  while (!Serial) { };

  msgn = sprintf(nodeName, "SNTM-P-ESP8266-%X", ESP.getChipId());
  msgn = snprintf(msgbuff, MSGBUFFLEN, "\n\n%s HX711 Propane Tank Gauge starting...", nodeName);
  outputMsg(msgbuff);

  Serial.println("Connect Wifi/mDNS/MQTT...");
  WiFi.forceSleepWake();
  connectWiFi();
  while (!setupMdns(nodeName)) {
    delay(5000);
  };

  do
  {
    hostEntry = findService("_mqtt", "_tcp");
    delay(5000);
  } while (hostEntry == -1);

  mqttTopic = "csc/homepa/tankmon_tst";
  while (!setupMQTT(MDNS.IP(hostEntry), MDNS.port(hostEntry), false, handleMQTTmsg)) {
    delay(5000);
  };

  scale.begin(DOUT, CLK);
  scale.set_scale(empericalScaleFactor);

  sendSample();
  sendSample();  // often the first, or just one, MQTT message does not go thru after deep sleep...so send 2.

  digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
  msgn = snprintf(msgbuff, MSGBUFFLEN, "\n\nEntering deep sleep for %e ms", DEEPSLEEPTIME);
  Serial.println(msgbuff);
  WiFi.forceSleepBegin();
  ESP.deepSleep(DEEPSLEEPTIME);
}
