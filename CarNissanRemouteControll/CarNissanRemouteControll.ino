/*
 Name:		CarNissanRemouteControll.ino
 Created:	21.01.2020 3:55:42
 Author:	Nikita
*/







#include <microDS18B20.h>
#include <ESP8266WiFi.h>
#include <timer2Minim.h>
#include <SoftwareSerial.h>




#define UNLOCK_BUTTON D0					// ������� �������������
#define LAUNCH_BUTTON D1					// ������� ������� ������
#define LOCK_BUTTON D3                        // ������� ����������      
#define RC_POWER D2                            // ������� ������
#define PORT_RX 13                             // TXd ������ = D7
#define PORT_TX 15                             // RXd ������ = D8 
#define PORT_INPUT_BATTERY A0                       // ���� ��� �0 ��� �������� ������� ����
#define DTR_PIN 12
#define DS18B20_PIN D4                       // ����  ��� ������� �����������
SoftwareSerial SIM800(PORT_RX, PORT_TX);
MicroDS18B20 DS18(DS18B20_PIN);
const  String ACCESS_PHONE="+79246375612";



const uint8_t MEASURE_COUNT = 10;                       // ������ ������� ��������� ���������� ������� (������� �������� �� N ���������)
const float MEASURE_RATIO = 0.0651;							// ������� u1=avarageU1/MEASURE_RATIO*5/1024/R2*(R1+R2), �������� ����������� = 5/1024/R2*(R1+R2)=0.022414801
const float DIODE_LOST = 0.75;

const float U1_RUNNING = 13.20;
const uint8_t U1_START_CHARGING = 9;		//voltage min
const int8_t U1_START_HEATING = -25;		//temp min
const int8_t U1_STOP_HEATING = 75;			//temp max
const bool START_BY_TEMP = 1;
const bool START_BY_VBAT = 0;



bool engineIsRunning,launchEngine,openCar,closeCar;
float temp, vBat;
String dtmfCode = "";                                   // ���������� ��� �������� �������� ������

timerMinim checkBatteryUTTimer(10000);
timerMinim sendDataToServerTimer(60000);
String _response = "";
void pressButton(uint8_t Button, uint16_t time=1000);

const char* ssid = "Svetov";
const char* password = "svetovtcar";

const char* server = "api.thingspeak.com";
String apiKey = "U348PR0IY901ORGJ";
int sent = 0;
WiFiClient client;

// the setup function runs once when you press reset or power the board
void setup() {
	//
	
	pinMode(RC_POWER, OUTPUT);  
	digitalWrite(RC_POWER, LOW);
	Serial.begin(57600);
	SIM800.begin(19200);
	SIM800.setTimeout(100);                       // ������������� ������� �������� ��������
	
	_response = sendATCommand("AT", true);              // �������� ������ �������
	_response = sendATCommand("AT+CLIP=1", true);  // �������� ���
	_response = sendATCommand("AT+DDET=1,0,0", true);   // �������� DTMF
	//AT+CSCLK=2 //power save auto mode,  AT&W_SAVE 
	setup_wifi();
}

// the loop function runs over and over again until power down or reset
void loop() {
	if (sendDataToServerTimer.isReady()){sendTeperatureTS();/*WiFi.disconnect();*/}
	if (checkBatteryUTTimer.isReady()) checkBatteryUT();
	if (launchEngine) { pressButton(LAUNCH_BUTTON, 6000); launchEngine = false; }
	if (openCar) { pressButton(UNLOCK_BUTTON); openCar = false; }
	if (closeCar) { pressButton(LOCK_BUTTON); closeCar = false; }
	//if (checkEngine && millis() >= checkEngineTime) checkEngineIsRunning();
	if (SIM800.available()) {                         // ���� �����, ���-�� ��������...
		_response = waitResponse();                       // �������� ����� �� ������ ��� �������
		Serial.println(">" + _response);                  // ������� ��������� ����� ���������
		int index = -1;
		do {                                             // ���������� ��������� ������ ��������� �����
			index = _response.indexOf("\r\n");              // �������� ����� �������� ������
			String submsg = "";
			if (index > -1) {                               // ���� ������� ������ ����, ������
				submsg = _response.substring(0, index);       // �������� ������ ������
				_response = _response.substring(index + 2);   // � ������� � �� �����
			}
			else {                                          // ���� ������ ��������� ���
				submsg = _response;                           // ��������� ������ - ��� ���, ��� �������� �� �����
				_response = "";                               // ����� ��������
			}
			submsg.trim();                                  // ������� ���������� ������� ������ � �����
			if (submsg != "") {                             // ���� ������ �������� (�� ������), �� ���������� ��� �
				Serial.println("submessage: " + submsg);
				if (submsg.startsWith("+DTMF:")) {            // ���� ����� ���������� � "+DTMF:" �����:
					String symbol = submsg.substring(7, 8);     // ����������� ������ � 7 ������� ������ 1 (�� 8)
					processingDTMF(symbol);                     // ������ ������� ��� �������� � ��������� �������
				}
				else if (submsg.startsWith("+CLIP:")){         // ��� �������� ������...
					if (submsg.indexOf(ACCESS_PHONE) > -1)
						sendATCommand("ATA", true);                 // ...�������� (��������� ������)
					else
						sendATCommand("ATH", true);                 // ...�� �������� (������ ������)
				}
				else if (submsg.length()<2)
					sendATCommand("AT", true);
				
				
			}
		} while (index > -1);                             // ���� ������ �������� ������ ������������
	}
	if (Serial.available()) {                          // ������� ������� �� Serial...
		SIM800.write(Serial.read());
	}
}


bool setup_wifi() {
	delay(10);
	// We start by connecting to a WiFi network
	Serial.println();
	Serial.print("Connecting to ");
	Serial.println(ssid);

	WiFi.begin(ssid, password);
	long connectionTimeout = millis();
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
		if (millis() - connectionTimeout > 10000ul)
		{
			Serial.println("Connection timeout");
			return false;
		}
	}

	Serial.println("");
	Serial.println("WiFi connected");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());
	return true;
}

/*void callback(char* topic, byte* payload, unsigned int length) {
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	for (int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
	}
	Serial.println();
}*/

void sendTeperatureTS()
{
	if (WiFi.status() != WL_CONNECTED) {
		if (!setup_wifi()) return;}
	//WiFiClient client; //check this string
	Serial.println("Step sendTeperatureTS");
	if (client.connect(server, 80)) { // use ip 184.106.153.149 or api.thingspeak.com
		Serial.println("WiFi Client connected ");

		String postStr = apiKey;
		postStr += "&field1=";
		postStr += String(temp);
		postStr += "&field2=";
		postStr += String(vBat);
		postStr += "\r\n\r\n";

		client.print("POST /update HTTP/1.1\n");
		client.print("Host: api.thingspeak.com\n");
		client.print("Connection: close\n");
		client.print("X-THINGSPEAKAPIKEY: " + apiKey + "\n");
		client.print("Content-Type: application/x-www-form-urlencoded\n");
		client.print("Content-Length: ");
		client.print(postStr.length());
		client.print("\n\n");
		client.print(postStr);
		delay(1000);
		Serial.println("Step sendTeperatureTS end");

	}//end if
	//sent++;
	client.stop();
	Serial.println("Step sendTeperatureTS exit");
}//end send


// ��������� ������� ��� ��������� ������ DTMF

void processingDTMF(String symbol) {
	Serial.println("Key: " + symbol);                   // ������� � Serial ��� ��������, ��� ������ �� ����������

	                                 // ���� 3 ��������� �������, ��������� ����������� �� �������
		if (symbol == "#") {
			if (dtmfCode == "123") { dtmfCode = "", launchEngine=true; }
			else if (dtmfCode =="456") { dtmfCode = "", openCar = true; }
			else if (dtmfCode == "789") { dtmfCode = "", closeCar = true; }
			 dtmfCode = "";                                 // ����� ������ ������� ���������� �������� ����������
		}
		else {
			dtmfCode += symbol;                               // ���, ��� ��������, �������� � ���� ������
		}
	
}
String sendATCommand(String cmd, bool waiting) {
	String _resp = "";                            // ���������� ��� �������� ����������
	Serial.println(cmd);                          // ��������� ������� � ������� �����
	SIM800.println("AT"); delay(100);			//----------�������� �� ���-------------
	SIM800.println(cmd);                          // ���������� ������� ������
	if (waiting) {                                // ���� ���������� ��������� ������...
		_resp = waitResponse();                     // ... ����, ����� ����� ������� �����
		// ���� Echo Mode �������� (ATE0), �� ��� 3 ������ ����� ����������������
		if (_resp.startsWith(cmd)) {                // ������� �� ������ ������������� �������
			_resp = _resp.substring(_resp.indexOf("\r\n", cmd.length()) + 2);
		}
		Serial.println(_resp);                      // ��������� ����� � ������� �����
	}
	return _resp;                                 // ���������� ���������. �����, ���� ��������
}

String waitResponse() {                         // ������� �������� ������ � �������� ����������� ����������
	String _resp = "";                            // ���������� ��� �������� ����������
	uint32_t _starttimer = millis();             // ���������� ��� ������������ �������� (10 ������)
	while (!SIM800.available() && (millis()-_starttimer) < 10000ul) {}; // ���� ������ 10 ������, ���� ������ ����� ��� �������� �������, ��...
	if (SIM800.available()) {                     // ���� ����, ��� ���������...
		_resp = SIM800.readString();                // ... ��������� � ����������
	}
	else {                                        // ���� ������ �������, ��...
		Serial.println("Timeout...");               // ... ��������� �� ���� �...
	}
	return _resp;                                 // ... ���������� ���������. �����, ���� ��������
}
//Temp & Battery check
void checkBatteryUT() {
	
	temp = DS18.requestTempAuto();
	vBat = VBatt();
	Serial.println("Engine temp: " + (String)temp);
	engineIsRunning = (vBat  > U1_RUNNING) ? true : false;
	//sendATCommand("AT", true);              // �������� ������ �������
	if (!engineIsRunning)
	{
		if (START_BY_TEMP && temp < U1_START_HEATING) { launchEngine = true; Serial.print("Start by temp, temp is: "); Serial.println(temp); }
		else if (START_BY_VBAT && vBat < U1_START_CHARGING) { launchEngine = true; Serial.print("Start by vBat, vBat is: "); Serial.println(vBat); }
	}
	
}
//Remoute Controller
void pressButton(uint8_t button, uint16_t time) {
	digitalWrite(RC_POWER, HIGH);
	delay(50);
	pinMode(button, OUTPUT);
	digitalWrite(button, LOW);
	delay(time);
	pinMode(button, INPUT);
	digitalWrite(button, LOW);
	delay(1000);
	digitalWrite(RC_POWER, LOW);
	Serial.println("Button complete");
	checkBatteryUTTimer.reset();

}
//Battery Voltage
float VBatt() {      // ���� ������ ���������� � ����������� �� ������� MEASURE_COUNT �������� (��������� �� ��������� ������� � �������)
	long sumU1 = 0;       // ���������� ��� ������������ ����� ���
	float utemp1 = 0;     // ���������� ����������
	for (int timeCount = 0; timeCount < MEASURE_COUNT; timeCount++) sumU1 += analogRead(PORT_INPUT_BATTERY); // ������������ ����� ���
	utemp1 = sumU1 / MEASURE_COUNT * MEASURE_RATIO + DIODE_LOST;
	Serial.println("Battery voltage: "+(String)utemp1);
	return utemp1;
}

