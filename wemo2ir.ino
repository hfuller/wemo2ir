#include <ESP8266WiFi.h>
#include <EEPROM.h>

#include "WemoSwitch.h"
#include "WemoManager.h"
#include "CallbackFunction.h"

#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>					//https://github.com/tzapu/WiFiManager

#include <IRremoteESP8266.h>


// prototypes
void toggle();
void save();
void parseString();

WemoManager wemoManager;
WemoSwitch *light = NULL;

IRsend irsend(4);
IRrecv irrecv(5);

ESP8266WebServer www(8080);

void setup()
{
	Serial.begin(115200);
	delay(100);
	Serial.println("wemo2ir");
	delay(1000);

	Serial.println("Starting wireless.");
	WiFiManager wifiManager; //Load the Wi-Fi Manager library.
	wifiManager.setTimeout(300); //Give up with the AP if no users gives us configuration in this many secs.
	//wifiManager.resetSettings();
	if(!wifiManager.autoConnect()) {
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		ESP.restart();
	}

	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	IPAddress ip = WiFi.localIP();
	Serial.println(ip);

	Serial.println("Starting WeMo");
	wemoManager.begin();
	// Format: Alexa invocation name, local port no, on callback, off callback
	light = new WemoSwitch("TV", 80, toggle, toggle);
	wemoManager.addDevice(*light);

	Serial.println("Starting IR");
	irsend.begin();
	irrecv.enableIRIn();

	Serial.println("Setting up second Web server");
	www.on("/toggle", [](){
		toggle();
		www.send(200, "text/plain", "done");
	});
	www.on("/save", [](){
		save();
		www.send(200, "text/plain", "done");
	});
	www.on("/debug", [](){
		EEPROM.begin(512);
		EEPROM.write(0,40);
		EEPROM.end();
	});
	www.on("/savegc", parseString);
	www.on("/forgetwifi", [](){
		WiFi.disconnect();
		ESP.restart();
	});
	www.begin();

	Serial.println("Startup complete");

}

void loop()
{
	Serial.print(WiFi.localIP());
	Serial.print(" - server loop... ");
	wemoManager.serverLoop();
	Serial.print(" - www.handleClient... ");
	www.handleClient();

	if ( WiFi.status() != WL_CONNECTED ) {
		Serial.println("we lost connection to Wi-Fi. Restart the device");
		delay(3000);
		ESP.restart();
	}
	
	Serial.println("connected and ready for another loop.");
	delay(250);

}

void toggle() {
		Serial.println("Time to toggle");

		unsigned int rawData2[256];
		Serial.print("loading from eeprom: ");
		EEPROM.begin(512);

		byte khz = EEPROM.read(0);
		byte length = EEPROM.read(1);
		for ( int i=1; i <= length; i++ ) {
			int eeploc = (i*2);
			int arrloc = i-1;
			rawData2[arrloc] = ( (EEPROM.read(eeploc)*256) + (EEPROM.read(eeploc+1)) );
			Serial.print(arrloc); Serial.print("="); Serial.print(rawData2[arrloc]); Serial.print(", ");
		}
		Serial.print("done... "); Serial.print(length); Serial.println(" values.");

		if ( khz == 0 ) {
			Serial.println("It's a GC code!!");
			Serial.print("Going to send "); Serial.print(rawData2[0]); Serial.print("Hz code of length "); Serial.println(length);
			irsend.sendGC(rawData2, length);
			Serial.println("done");
			
		} else {
			//unsigned int  rawData[67] = {4550,4400, 650,1600, 650,1600, 650,1650, 650,500, 650,500, 650,500, 650,500, 650,500, 650,1600, 650,1600, 650,1650, 650,500, 650,500, 650,500, 650,500, 650,500, 650,500, 650,1650, 650,500, 650,500, 650,500, 650,500, 650,500, 650,500, 650,1650, 650,500, 650,1600, 650,1600, 650,1600, 650,1600, 650,1600, 650,1600, 650};  // SAMSUNG E0E040BF
			//Serial.print("sizeof: "); Serial.println(sizeof(rawData)/sizeof(rawData[0]));
			//irsend.sendRaw(rawData, (sizeof(rawData)/sizeof(rawData[0])), 38);

			Serial.println("It's a raw code");
			Serial.print("Going to send "); Serial.print(khz); Serial.print("kHz code, of length "); Serial.println(length);
			irsend.sendRaw(rawData2, length, khz);
			Serial.println("done");
		
		}

		EEPROM.end();
}

void save() {
	Serial.println("Time to receive and save to eeprom");
	decode_results * results = new decode_results();
	if ( irrecv.decode(results) ) {
		Serial.println("received");
		EEPROM.begin(512);
		Serial.println("EEPROM initialized.");
		switch (results->decode_type) {
			case SONY: EEPROM.write(0,40); //kHz
			case RC5: case RC6: EEPROM.write(0,36);
			case DISH: EEPROM.write(0,56);
			case PANASONIC: EEPROM.write(0,35);
			default: EEPROM.write(0, 38);
		}
		Serial.print("kHz written: "); Serial.println(EEPROM.read(0));
		EEPROM.write(1, results->rawlen - 1); //how long is it
		Serial.print("Writing actual code: ");
		for (int i = 1;  i < results->rawlen;  i++) {
			int loc = (i*2);
			unsigned int value = results->rawbuf[i]*USECPERTICK;
			Serial.print(loc); Serial.print("="); Serial.print(value);
			EEPROM.write(loc,   (value>>8) & 0xFF);
			EEPROM.write(loc+1, value & 0xFF);
			Serial.print(", ");
		}
		Serial.println("done.");
		EEPROM.end();
	}	
}

void parseString() {
	unsigned int *codeArray;
	String str = www.arg("code");
	int nextIndex;
	int codeLength = 1;
	int currentIndex = 0;
	nextIndex = str.indexOf(',');

	// change to do/until and remove superfluous repetition below...
	Serial.println("reading GC code from the interbutt: ");
	while (nextIndex != -1) {
		if (codeLength > 1) {
			codeArray = (unsigned int*) realloc(codeArray, codeLength * sizeof(unsigned int));
		} else {
			codeArray = (unsigned int*) malloc(codeLength * sizeof(unsigned int));
		}

		codeArray[codeLength-1] = (unsigned int) (str.substring(currentIndex, nextIndex).toInt());
		Serial.print(codeArray[codeLength-1]); Serial.print(", ");
	
		codeLength++;
		currentIndex = nextIndex + 1;
		nextIndex = str.indexOf(',', currentIndex);			 
	}
	codeArray = (unsigned int*) realloc(codeArray, codeLength * sizeof(unsigned int));
	codeArray[codeLength-1] = (unsigned int) (str.substring(currentIndex, nextIndex).toInt());
	Serial.print(codeArray[codeLength-1]);
	Serial.print("... done. size read: "); Serial.println(codeLength);
	EEPROM.begin(512);
	EEPROM.write(0,0); Serial.println("Wrote gc indicator");
	EEPROM.write(1,codeLength); Serial.println("wrote code length");
	Serial.print("Writing code to eeprom: ");
	for ( int i = 0; i < codeLength; i++ ) {
		int loc = (i+1)*2;
		unsigned int value = codeArray[i];
		Serial.print(loc); Serial.print("="); Serial.print(value);
		EEPROM.write(loc,   (value>>8) & 0xFF);
		EEPROM.write(loc+1, value & 0xFF);
		Serial.print(", ");
	}
	Serial.println("done.");
	EEPROM.end();

	www.send(200, "text/plain", "done");
}
