//#include "ESP8266WiFi.h"
//#include "ESP8266WebServer.h"
#include "Arduino.h"
#include "DNSServer.h"
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

class PrevSize {
	File file;
	int size;
	char *name;
public:
	
	PrevSize() {
		if (!LittleFS.begin()) {
			Serial.println("An Error has occurred while mounting LittleFS!");
			return;
		}
		name = (char *) "/lastSize";
		size = getSize();
	}
	
	int getSize() {
		int tmp;
		file = LittleFS.open(name, "r");
		if (file) tmp = strtol(file.readString().c_str(), nullptr, 10);
		file.close();
		return tmp;
	}
	
	void updateSize(int i) {
		file = LittleFS.open(name, "w");
		file.print(i);
		file.close();
	}
	
	void clearSize() {
		updateSize(99999);
	}
};

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

void addEvents(char *s) {
	if (strlen(s) == 0) {
		Serial.println("No data!");
		return;
	}
	
	//	Setup timezone
	waitForSync();
	Timezone date;
	date.setLocation(timezone(s));
	date.setDefault();
	
	char *pch = strtok(s, "\n");
	while (pch != nullptr) {
		if (strstr(pch, "BEGIN:VEVENT")) eventList.push_back(*new Event);
		else if (strstr(pch, "DTSTART")) eventList.back().time = parseDT(pch);
		else if (strstr(pch, "SUMMARY")) eventList.back().name = strchr(pch, ':') + 1;
		
		pch = strtok(nullptr, "\n");
	}
	
	//	Sort by date
	std::sort(eventList.begin(),
	          eventList.end(),
	          [](Event a, Event b) { return (a.time < b.time); });
	
	for (Event x: eventList)
		Serial.printf("%s  ~  %s\n", dateTime(x.time).c_str(), x.name);
}

void getTasks() {
	std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
	HTTPClient https;
	PrevSize prev;
	
	prev.clearSize();
	
	//	TODO Abstract this
	char url[] = "";
	
	if (!strlen(url)) {
		Serial.println("TickTick URL not set!");
		return;
	}
	
	client->setSSLVersion(BR_TLS12);
	client->setInsecure();
	
	if (!https.begin(*client, url)) return;
	
	Serial.print("[HTTPS] GET...\n");
	//  Start connection and send HTTP header
	int httpCode = https.GET();
	if (httpCode != HTTP_CODE_OK) return;
	
	//  Length of content (-1 when the server doesn't send a 'Content-Length' header)
	int len = https.getSize();
	
	//  Same size check. Lazy, but cheap and effective.
	if (len == prev.getSize()) return;
	
	prev.updateSize(len);
	
	Serial.printf("Size: %d\n", len);
	
	// create buffer for read
	static char buff[256] = {0};
	
	String content = "";
	
	// read all data from server
	while (https.connected() && (len > 0 || len == -1)) {
		// get available data size
		size_t size = client->available();
		
		if (size) {
			// read up to 256 byte
			int c = (int) client->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
			content.concat(buff);
			if (len) len -= c;
		}
		delay(1);
		memset(buff, 0, 256); // Clear buffer
	}
	
	Serial.print("[HTTPS] connection closed or file end.\n");
	
	https.end();
	
	addEvents(const_cast<char *>(content.c_str()));
}

void clearScreen() {
	for (int i = 0; i < 100; ++i) {
		Serial.println();
	}
	Serial.write(27);
	Serial.print("[H");
}

void setup() {
	Serial.begin(9600);
	
	clearScreen();
	
	wifiSetup();
	getTasks();
	
	Serial.println("\nBed time...\n");
	
	EspClass::deepSleep(10e6);
}

// Because of deep sleep, setup acts like loop
void loop() {}