/*

   2020-06-07 C. Collins created. Derived code from several sources including SparkFuns HX711 breakout example code
   2020-09-26 C. Collins, ported to AdaFruit Huzzah Feather ESP8266 due to deep sleep problems with cheap NodeMCU's. The cheaper
						  NodeMCU's tend to "zombie" (go to sleep and never wake up).
   2020-11-05 C. Collins, moved to github
   2020-11-08 C. Collins, moved to Visual Studio/Visual Micro from Arduino IDE

  Todo:

  - Firmware Update. Normal OTA wont work so add logic to check for firmware update in setup().
  - Config file update. Ditto.



*/

ADC_MODE(ADC_VCC);

#include "HX711.h"
#include "cscNetServices.h"
#include "propanemon.h";

int chipID = 0;

HX711 scale;
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


float checkTank()
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

	return (propaneTank25lb.percentFull);
}

float battV() {
	float vcc = ESP.getVcc();
	float bV = vcc / 1000;
	Serial.print("\nBatt V =");
	Serial.println(bV);

	return (bV);
}

void sendSample()
{
	bool rc = false;

	if (!mqttClient.connected())
	{
		connectMQTT(false, mqttTopicCtrl, MDNS.IP(hostEntry));
		delay(3000);
	};

	checkTank();

	sampleDoc["msgtype"] = "T";  // tank msg
	sampleDoc["node"] = chipID;
	sampleDoc["battV"] = battV();
	sampleDoc["tT"] = propaneTank25lb.tankType;
	sampleDoc["t"] = propaneTank25lb.tankNum;
	sampleDoc["pF"] = (round(propaneTank25lb.percentFull * 100) / 100);
	serializeJson(sampleDoc, msgbuff);
	rc = mqttClient.publish(mqttTopicData, msgbuff);
	if (!rc) Serial.print("MQTT publish failed!");

	if (debug)
	{
		Serial.print("\nMQTT msg published: ");
		Serial.println(msgbuff);
	}
}

long int setSleepTime()   // return sleep time in seconds
{
	long int sleepTime = 0;

	if (debug)
	{
		Serial.println("in setSleepTime()");
		Serial.println(checkTank());
		Serial.println(sleepTimeFloor);
	}


	if (checkTank() > sleepTimeFloor)
	{
		sleepTime = sleepTimeAboveFloor;
	}
	else
	{
		sleepTime = sleepTimeBelowFloor;
	}

	if (debug)
	{
		Serial.print("\nsleepTime = ");
		Serial.println(sleepTime);
	}

	return (sleepTime);
}

void setup()
{
	delay(5000);                             // allow for intervention
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, LOW);

	Serial.begin(9600);
	while (!Serial) {};

	chipID = ESP.getChipId();
	msgn = sprintf(nodeName, "SNTM-P-ESP8266-%X", chipID);
	msgn = snprintf(msgbuff, MSGBUFFLEN, "\n\n%s HX711 Propane Tank Gauge starting...", nodeName);
	outputMsg(msgbuff);

	if (!loadConfig())
	{
		Serial.print("Halting.");
		while (true) {};      // do nothing
	}
	Serial.println("Config loaded");


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

	while (!setupMQTT(MDNS.IP(hostEntry), MDNS.port(hostEntry), false, mqttTopicCtrl, handleMQTTmsg)) {
		delay(5000);
	};

	scale.begin(DOUT, CLK);
	scale.set_scale(empericalScaleFactor);

	sendSample();
	sendSample();  // often the first MQTT message does not go thru after deep sleep...so send 2.

	digitalWrite(LED_BUILTIN, HIGH);
}

void loop()
{
	msgn = sprintf(msgbuff, "Entering deep sleep for %i seconds", setSleepTime());
	outputMsg(msgbuff);
	ESP.deepSleep(setSleepTime() * 1e6);
}
