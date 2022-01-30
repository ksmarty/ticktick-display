#include "Arduino.h"
//#include "ESP8266WiFi.h"
#include "DNSServer.h"
//#include "ESP8266WebServer.h"
#include "WiFiManager.h"
#include "ESP8266HTTPClient.h"
#include "WiFiClientSecureBearSSL.h"
#include "ezTime.h"
#include "LittleFS.h"

struct Event {
	char *name;
	time_t time;
};

std::vector<Event> eventList;

int prevSize = 0;

//	WiFi Setup
void wifiSetup() {
	WiFiManager wifiManager;
	wifiManager.autoConnect("TickTick", "password");
	Serial.println("Connected!");
}

char *timezone(char *s) {
	//	Get Timezone in Olson format
	char *tzid = strstr(s, "TZID:");
	const char *start = strchr(tzid, ':') + 1;
	char *tz = (char *) "\0";
	strncat(tz, start, strcspn(start, "\n") - 1);
	return tz;
}

time_t parseDT(char *s) {
	const char *time = strchr(s, ':') + 1;
	
	uint8_t hr = 0, min = 0, sec = 0, day, mn;
	uint16_t yr;
	char buf[5];
	
	//	Set year
	strncpy(buf, time, 4);
	buf[4] = '\0';
	yr = (uint16_t) strtol(buf, nullptr, 10);
	
	//	Set month
	strncpy(buf, time + 4, 2);
	buf[2] = '\0';
	mn = (uint8_t) strtol(buf, nullptr, 10);
	
	//	Set day
	strncpy(buf, time + 6, 2);
	buf[2] = '\0';
	day = (uint8_t) strtol(buf, nullptr, 10);
	
	char *T = strchr(time, 'T');
	
	//	Parse Time
	if (T) {
		strncpy(buf, T + 1, 2);
		buf[2] = '\0';
		hr = (uint8_t) strtol(buf, nullptr, 10);
		
		strncpy(buf, T + 3, 2);
		buf[2] = '\0';
		min = (uint8_t) strtol(buf, nullptr, 10);
		
		strncpy(buf, T + 5, 2);
		buf[2] = '\0';
		sec = (uint8_t) strtol(buf, nullptr, 10);
	}
	
	return makeTime(hr, min, sec, day, mn, yr);
}

bool compEvent(Event a, Event b) {
	return (a.time < b.time);
}

void addEvents(char *s) {
	//	Setup timezone
	waitForSync();
	Timezone date;
	date.setLocation(timezone(s));
	date.setDefault();
	
	char *pch = strtok(s, "\n");
	while (pch != nullptr) {
		if (strstr(pch, "BEGIN:VEVENT")) {
			eventList.push_back(*new Event);
		} else if (strstr(pch, "DTSTART")) {
			eventList.back().time = parseDT(pch);
		} else if (strstr(pch, "SUMMARY")) {
			eventList.back().name = strchr(pch, ':') + 1;
		}
		
		pch = strtok(nullptr, "\n");
	}
	
	//	Sort by date
	std::sort(eventList.begin(), eventList.end(), compEvent);
	
	for (Event x: eventList)
		Serial.printf("%s  ~  %s\n", dateTime(x.time).c_str(), x.name);
}

void getTasks() {
	std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
	HTTPClient https;
	
	int lastSize;
	File file = LittleFS.open("/lastSize", "r");
	if (file) {
		lastSize = strtol(file.readString().c_str(), nullptr, 10);
	}
	file.close();
	
	//	TODO Abstract this
	char url[] = "";
	
	client->setSSLVersion(BR_TLS12);
	client->setInsecure();
	
	if (!https.begin(*client, url)) return;
	
	Serial.print("[HTTPS] GET...\n");
	// start connection and send HTTP header
	int httpCode = https.GET();
	if (httpCode != HTTP_CODE_OK) return;
	
	// length of content (-1 when the server doesn't send a 'Content-Length' header)
	int len = https.getSize();
	
	if (len == lastSize) return;
	file = LittleFS.open("/lastSize", "w");
	file.print(len);
	file.close();
	
	Serial.printf("Size: %d\n", len);
	
	// create buffer for read
	static uint8_t buff[256] = {0};
	
	String content = "";
	
	// read all data from server
	while (https.connected() && (len > 0 || len == -1)) {
		// get available data size
		size_t size = client->available();
		
		if (size) {
			// read up to 256 byte
			int c = client->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
			content.concat((char *) buff);
			if (len > 0) len -= c;
		}
		delay(1);
		memset(buff, 0, 256); // Clear buffer
	}
	
	Serial.print("[HTTPS] connection closed or file end.\n");
	
	https.end();
	
	addEvents((char *) content.c_str());
}

void setup() {
	Serial.begin(9600);
	
	// Initialize SPIFFS
	if (!LittleFS.begin()) {
		Serial.println("An Error has occurred while mounting LittleFS!");
		return;
	}
	
	wifiSetup();
	getTasks();
	
	Serial.println("Bed time...");
	EspClass::deepSleep(10e6);
}

void loop() {
}