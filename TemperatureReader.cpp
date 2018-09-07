#include <PubSubClient.h>
#include "D:\Onedrive\GitHub\NtpClient\src\NTPClientLib.h"
#include <D:\Onedrive\GitHub\ConfigManager\src\ConfigManager.h>

#define DEBUG_APPLICATION //Uncomment this to enable debug messages over serial port

#define DBG_PORT Serial

#ifdef DEBUG_APPLICATION
#define DEBUGLOG(...) DBG_PORT.printf(__VA_ARGS__)
#else
#define DEBUGLOG(...)
#endif

// NTP
#define SHOW_TIME_PERIOD 5000
#define NTP_TIMEOUT 1500
#define CONFIG_NTPHOSTNAME_LENGTH 64

bool ntpStarted = false;
int8_t minutesTimeZone = 0;

void ntpSetup();
void ntpLoop();

// ConfigManager
#define CONFIG_MQTTHOSTNAME_LENGTH 64

struct Config {
	char MQTTserver[CONFIG_MQTTHOSTNAME_LENGTH];
	char NTPserver[CONFIG_NTPHOSTNAME_LENGTH];
	int8_t timezone;
} config;

ConfigManager configManager;
bool wifiConnected = false;

void configManagerSetup();
void configManagerLoop();

// MQQT
#define MQTT_PORT 1883

WiFiClient espClient;
PubSubClient mqttClient(espClient);

void mqttSetup();
void mqttLoop();

//
void onSTAGotIP(WiFiEventStationModeGotIP ipInfo) {
	Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
	Serial.printf("Connected: %s\r\n", WiFi.status() == WL_CONNECTED ? "yes" : "no");
}

void onSTAConnected(WiFiEventStationModeConnected ipInfo) {
	Serial.printf("Connected to %s\r\n", ipInfo.ssid.c_str());
	wifiConnected = true;
}

// Manage network disconnection
void onSTADisconnected(WiFiEventStationModeDisconnected event_info) {
	Serial.printf("Disconnected from SSID: %s\n", event_info.ssid.c_str());
	Serial.printf("Reason: %d\n", event_info.reason);
	//NTP.stop(); // NTP sync can be disabled to avoid sync errors
	wifiConnected = false;
}

// main setup
void setup() {
	static WiFiEventHandler e1, e2, e3;

	Serial.begin(115200);
	Serial.println("");

	e1 = WiFi.onStationModeGotIP(onSTAGotIP); // As soon WiFi is connected, start NTP Client
	e2 = WiFi.onStationModeDisconnected(onSTADisconnected);
	e3 = WiFi.onStationModeConnected(onSTAConnected);

	// creates WiFi connection
	configManagerSetup();

	ntpSetup();

	// starts mqtt broker
	mqttSetup();
}


// main loop
time_t lastTimePrinted = 0;
void loop() {

	configManagerLoop();

	ntpLoop();

	mqttLoop();

	if (configManager.getMode() == api && now()  - lastTimePrinted >= 2 ) {
		//DEBUGLOG("time: %s\n\r", NTP.getTimeStr().c_str());
		lastTimePrinted = now();
	}


}


///////////////////////////////////////////////////////////////////////////////////////////////
//
// NTP
//
///////////////////////////////////////////////////////////////////////////////////////////////
bool ntpSyncEventTriggered = false;
NTPSyncEvent_t ntpLastEvent;


void ntpProcessSyncEvent(NTPSyncEvent_t);

void ntpSetup() {

	ntpStarted = false;

	NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {
		ntpLastEvent = event;
		ntpSyncEventTriggered = true;
	});

}

void ntpLoop() {

	if (WiFi.status() != WL_CONNECTED) return;

	if (!ntpStarted) {

		if (!NTP.setInterval(63))
			return;
		if (!NTP.setNTPTimeout(NTP_TIMEOUT))
			return;
		if (!NTP.begin(config.NTPserver, config.timezone, true, minutesTimeZone))
			return;
		Serial.printf("NTP started: server = \"%s\"\r\n", config.NTPserver);
		ntpStarted = true;

	}

	if (ntpSyncEventTriggered) {
		ntpProcessSyncEvent(ntpLastEvent);
		ntpSyncEventTriggered = false;
	}
}

void ntpProcessSyncEvent(NTPSyncEvent_t ntpEvent) {
	if (ntpEvent < 0) {
		Serial.printf("Time Sync error: %d", ntpEvent);

		switch (ntpEvent) {
		case noResponse:
			Serial.printf(" (%s)\r\n", "NTP server not reachable");
			break;
		case invalidAddress:
			Serial.printf(" (%s)\r\n", "Invalid NTP server address");
			break;
		case errorSending:
			Serial.printf(" (%s)\r\n", "Error sending request");
			break;
		case responseError:
			Serial.printf(" (%s)\r\n", "NTP response error");
			break;
		default:
			Serial.printf(" (%s)\r\n", "Unknown error code");
			break;
		}
	} else {
		switch (ntpEvent) {
		case timeSyncd:
			Serial.printf("Got NTP time: %s\r\n", NTP.getTimeDateString(NTP.getLastNTPSync()).c_str());
			break;
		case requestSent:
			Serial.printf("Request sent: %d\r\n", ntpEvent);
			break;
		default:
			Serial.printf("Unknown code: %d\r\n", ntpEvent);
			break;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT
//
///////////////////////////////////////////////////////////////////////////////////////////////
bool mqttConnect(const char*);
void mqttCallback(char*, byte*, unsigned int);

time_t lastMsg = 0;
int value = 0;
char mqttTopic[256];

void mqttSetup() {
	mqttClient.setServer(config.MQTTserver, MQTT_PORT);
	mqttClient.setCallback(mqttCallback);

	sprintf(mqttTopic,"%s/%s","/thijssen/smarthome", WiFi.hostname().c_str());
}

void mqttLoop() {

	if (wifiConnected && ntpStarted && !mqttClient.connected()) {
		mqttConnect(WiFi.hostname().c_str());
	}

	if (mqttClient.connected()) {
		mqttClient.loop();

		if (now() - lastMsg >= 5) {
			lastMsg = now();
			++value;
			char msg[128];
			snprintf(msg, sizeof(msg), "hello world #%ld (%s)", value, NTP.getTimeStr().c_str());
			//Serial.printf("Publish message: %s for topic %s\r\n", msg, mqttTopic);
			mqttClient.publish(mqttTopic, msg);
		} else {
			//Serial.printf("now() = %l, lastMsg = %l\n\r", now(), lastMsg);
		}

	}

}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char) payload[i]);
	}
	Serial.println();

	// Switch on the LED if an 1 was received as first character
	if ((char) payload[0] == '1') {
		digitalWrite(BUILTIN_LED, LOW); // Turn the LED on (Note that LOW is the voltage level
		// but actually the LED is on; this is because
		// it is acive low on the ESP-01)
	} else {
		digitalWrite(BUILTIN_LED, HIGH); // Turn the LED off by making the voltage HIGH
	}
}

#define MQQT_CONNECT_INTERVAL 2
int mqttRetries = 0;
time_t mqttLastConnectAttempt = 0;
bool mqttConnect(const char* identifier) {

	//
	if (now() - mqttLastConnectAttempt < MQQT_CONNECT_INTERVAL) return false;

	mqttLastConnectAttempt = now();
	DEBUGLOG("mqttConnect(%s)\n\r", identifier);

	Serial.printf("Attempting connection to Message Broker (MQTT server %s) #%ld", config.MQTTserver, mqttRetries);
	// Attempt to connect
	if (mqttClient.connect(identifier)) {
		Serial.println("  connected");
		// Once connected, publish an announcement...
		char msg[256];
		sprintf(msg, "Connected device %s (%s)", WiFi.hostname().c_str(), WiFi.localIP().toString().c_str());
		mqttClient.publish("/thijssen/smarthome/connected", msg);
		// ... and subscribe
		mqttClient.subscribe("inTopic");
	} else {
		Serial.print("failed, rc=");
		Serial.println(mqttClient.state());

		mqttRetries++;
	}
	return (mqttClient.connected());
}

///////////////////////////////////////////////////////////////////////////////////////////////
//
// ConfigManager
//
///////////////////////////////////////////////////////////////////////////////////////////////
void mqttServerNameChangedCallback(const char*, const char*, const char*);
void ntpServerNameChangedCallback(const char*, const char*, const char*);
void timezoneChangedCallback(const char*, const int8_t*, const int8_t*);
void createCustomRoute(WebServer *);

void configManagerSetup() {

	// Setup config manager
	configManager.setAPName("TemperatureReader");
	configManager.setAPFilename("/index.html");

	configManager.setAPCallback(createCustomRoute);
	configManager.setAPICallback(createCustomRoute);

	configManager.begin(config);

	configManager.addParameter("mqttServer", config.MQTTserver, CONFIG_MQTTHOSTNAME_LENGTH, both, &mqttServerNameChangedCallback);
	configManager.addParameter("ntpServer", config.NTPserver, CONFIG_NTPHOSTNAME_LENGTH, both, &ntpServerNameChangedCallback);
	configManager.addParameter<int8_t>("timezone", &config.timezone, both, &timezoneChangedCallback);
}

void configManagerLoop() {
	configManager.loop();
}

void timezoneChangedCallback(const char* name, const int8_t *oldValue, const int8_t *newValue) {
	Serial.printf("parameter '%s' changed from \"%d\" to \"%d\"\n\r", name, *oldValue, *newValue);
	NTP.setTimeZone(*newValue);
}

void mqttServerNameChangedCallback(const char* name, const char* oldValue, const char* newValue) {
	Serial.printf("parameter '%s' changed from \"%s\" to \"%s\"\n\r", name, oldValue, newValue);
}

void ntpServerNameChangedCallback(const char* name, const char* oldValue, const char* newValue) {
	Serial.printf("parameter '%s' changed from \"%s\" to \"%s\"\n\r", name, oldValue, newValue);
}

void createCustomRoute(WebServer *server) {
	server->on("/custom", HTTPMethod::HTTP_GET, [server]() {
		server->send(200, "text/plain", "Hello, World!");
	});
}
