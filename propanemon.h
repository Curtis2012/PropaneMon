//
// propanemon.h
//
// Propane tank monitor header file
//
//
/*
 * 2020-09-28 Created
 * 
 *
 */

#include "FS.h"
#include <ArduinoJson.h>
#include <TimeLib.h>
#include "timer.h"          // by Michael Contreras

#define PROPANEMONCFGFILE  "/propanemoncfg.json"
#define JSONCONFIGDOCSIZE  512
#define DOUT  14
#define CLK   12

byte configBuff[JSONCONFIGDOCSIZE];
File configFile;
StaticJsonDocument<JSONCONFIGDOCSIZE> configDoc;

const char* sitename = " ";

bool dst = false;


long int deepSleepTime = 0;   // deep sleep time in ms
float kgToLbsFactor = -2.20462F;
float empericalScaleFactor = -23680.00;  // derived by testing with SparkFuns calibrate sketch...add dynamic calibration later...
int sleepTimeAboveFloor = 0;   // how long to sleep if tank above % full "floor" in seconds
int sleepTimeBelowFloor = 0;   // how long to sleep if tank below % full "floor" in seconds
int sleepTimeFloor = 0;  // tank level floor in nearest whole % full (example 25, not 0.25)

struct propaneTank_t
{
  const char* tankType = "P";
  int tankNum = 4;
  float capacityKg = 11.33;  // 25lbs = 11.33kg
  float tareKg = 6.486;       // 14.3lbs = 6.486kg
  float netPropaneWt = 0;
  float scaleWeight = 0;
  float percentFull = 0;
  int loAlarm = 0;
};

propaneTank_t propaneTank25lb;

#define MAXPAYLOADSIZE 256
#define MAXJSONSIZE 256
#define MSGBUFFSIZE 512

// Alarm related bit masks & structs

#define CLEARALARMS   0b00000000
#define HIALARM       0b00000001
#define LOALARM       0b00000010
#define MAXDEPTH      0b00000100
#define NUMALARMS 4

float loAlarmFactor = 0.10;
float hiAlarmFactor = 1.10;

struct alarm {
	std::uint8_t alarmType;
	char alarmName[10];
};

alarm alarms[NUMALARMS] = {
  {HIALARM, "HI"},
  {LOALARM, "LO"},
  {MAXDEPTH, "MAXDEPTH"},
  {CLEARALARMS, "CLEARALL"}
};

bool globalAlarmFlag = false;

// JSON Message Definition

StaticJsonDocument<MAXJSONSIZE> propanemsg;

/*  JSON Message Structure
  {
	"n"         // node name
	"t"         // tank number
	"tT"        // tank type
    "pF"        // percent full
	"aF"        // alarm flags
  }
*/


//
// Functions
//

bool loadConfig()
{
	DeserializationError jsonError;
	size_t size = 0;
	int t = 0;

	Serial.println("Mounting FS...");
	if (!SPIFFS.begin())
	{
		Serial.println("Failed to mount file system");
		return false;
	}
	else Serial.println("Mounted file system");

    Serial.print("\nOpening config file: ");
	Serial.print(PROPANEMONCFGFILE);
	configFile = SPIFFS.open(PROPANEMONCFGFILE, "r");
	if (!configFile)
	{
		Serial.println("Failed to open config file");
		if (!SPIFFS.exists(PROPANEMONCFGFILE)) Serial.println("File does not exist");
		return false;
	}

	size = configFile.size();
	Serial.print("File size = ");
	Serial.println(size);

	configFile.read(configBuff, size);

	jsonError = deserializeJson(configDoc, configBuff);
	if (jsonError)
	{
		Serial.println("Failed to parse config file");
		return false;
	}
	serializeJsonPretty(configDoc, Serial);

	sitename = configDoc["site"]["sitename"];
	pssid = configDoc["site"]["pssid"];
	timeZone = configDoc["site"]["timezone"];
	dst = configDoc["site"]["dst"];
	ppwd = configDoc["site"]["ppwd"];
	wifiTryAlt = configDoc["site"]["usealtssid"];
	assid = configDoc["site"]["altssid"];
	apwd = configDoc["site"]["altpwd"];
	mqttTopicData = configDoc["site"]["mqtt_topic_data"];
	mqttTopicCtrl = configDoc["site"]["mqtt_topic_ctrl"];	
	mqttUid = configDoc["site"]["mqtt_uid"];
	mqttPwd = configDoc["site"]["mqtt_pwd"];
	debug = configDoc["site"]["debug"];
	sleepTimeAboveFloor = configDoc["site"]["sleeptimeabovefloor"];
	sleepTimeBelowFloor = configDoc["site"]["sleeptimebelowfloor"];
	sleepTimeFloor = configDoc["site"]["sleeptimefloor"];
	empericalScaleFactor = configDoc["site"]["empericalscalefactor"];

	Serial.println("Config loaded");
	
	msgn = snprintf(msgbuff, MSGBUFFLEN, "\nCongif dump:\n sitename %s\npssid %s\ntimezone %i", sitename, pssid, timeZone);
    Serial.println(msgbuff);
	msgn = snprintf(msgbuff, MSGBUFFLEN, "\ndst %i\nppwd %s\nwifiTryAlt %i\nassid %s\napwd %s", dst, ppwd, wifiTryAlt, assid, apwd);
	Serial.println(msgbuff);
	
	Serial.println(mqttTopic);
	Serial.println(mqttUid);
	Serial.println(mqttPwd);
	Serial.println(debug);	
	Serial.println(deepSleepTime);
	Serial.println(empericalScaleFactor);
	
    msgn = snprintf(msgbuff, MSGBUFFLEN, "\nmqttTopic %s\nmqttUid %s", mqttTopicData, mqttUid);
    Serial.println(msgbuff);
	msgn = snprintf(msgbuff, MSGBUFFLEN, "\n\nmqttPwd %s\ndebug %i", mqttPwd, debug);
	Serial.println(msgbuff);
	msgn = snprintf(msgbuff, MSGBUFFLEN, "\nsleepTimeAboveFloor %i\nsleeptimeBelowfloor %i", sleepTimeAboveFloor, sleepTimeBelowFloor);
	Serial.println(msgbuff);
	msgn = snprintf(msgbuff, MSGBUFFLEN, "\nsleeptimefloor %i", sleepTimeFloor);
	Serial.println(msgbuff);
	
	return true;
}


int mapAlarm(std::uint8_t alarmType)
{
	switch (alarmType) {
	case HIALARM:
		return (0);

	case LOALARM:
		return (1);

	case MAXDEPTH:
		return (2);

	case CLEARALARMS:
		return (3);

	default: return (-1);

	}
}


