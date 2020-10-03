/*
 Name:		CarNissanRemouteControll.ino
 Created:	21.01.2020 3:55:42
 Author:	Nikita
*/







#include <microDS18B20.h>
#include <ESP8266WiFi.h>
#include <timer2Minim.h>
#include <SoftwareSerial.h>




#define UNLOCK_BUTTON D0					// Клавиша разблокировки
#define LAUNCH_BUTTON D1					// Средняя клавиша пульта
#define LOCK_BUTTON D3                        // Клавиша блокировки      
#define RC_POWER D2                            // Питание пульта
#define PORT_RX 13                             // TXd модема = D7
#define PORT_TX 15                             // RXd модема = D8 
#define PORT_INPUT_BATTERY A0                       // порт АЦП А0 для контроля питания авто
#define DTR_PIN 12
#define DS18B20_PIN D4                       // порт  для датчика температуры
SoftwareSerial SIM800(PORT_RX, PORT_TX);
MicroDS18B20 DS18(DS18B20_PIN);
const  String ACCESS_PHONE="+79246375612";



const uint8_t MEASURE_COUNT = 10;                       // циклов периода измерения напряжения батареи (среднее значение из N измерений)
const float MEASURE_RATIO = 0.0651;							// Формула u1=avarageU1/MEASURE_RATIO*5/1024/R2*(R1+R2), итоговый коэффициент = 5/1024/R2*(R1+R2)=0.022414801
const float DIODE_LOST = 0.75;

const float U1_RUNNING = 13.20;
const uint8_t U1_START_CHARGING = 9;		//voltage min
const int8_t U1_START_HEATING = -25;		//temp min
const int8_t U1_STOP_HEATING = 75;			//temp max
const bool START_BY_TEMP = 1;
const bool START_BY_VBAT = 0;



bool engineIsRunning,launchEngine,openCar,closeCar;
float temp, vBat;
String dtmfCode = "";                                   // Переменная для хранения вводимых данных

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
	SIM800.setTimeout(100);                       // Устанавливаем меньшее значение таймаута
	
	_response = sendATCommand("AT", true);              // Проверка общего статуса
	_response = sendATCommand("AT+CLIP=1", true);  // Включаем АОН
	_response = sendATCommand("AT+DDET=1,0,0", true);   // Включаем DTMF
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
	if (SIM800.available()) {                         // Если модем, что-то отправил...
		_response = waitResponse();                       // Получаем ответ от модема для анализа
		Serial.println(">" + _response);                  // Выводим поученную пачку сообщений
		int index = -1;
		do {                                             // Перебираем построчно каждый пришедший ответ
			index = _response.indexOf("\r\n");              // Получаем идекс переноса строки
			String submsg = "";
			if (index > -1) {                               // Если перенос строки есть, значит
				submsg = _response.substring(0, index);       // Получаем первую строку
				_response = _response.substring(index + 2);   // И убираем её из пачки
			}
			else {                                          // Если больше переносов нет
				submsg = _response;                           // Последняя строка - это все, что осталось от пачки
				_response = "";                               // Пачку обнуляем
			}
			submsg.trim();                                  // Убираем пробельные символы справа и слева
			if (submsg != "") {                             // Если строка значимая (не пустая), то распознаем уже её
				Serial.println("submessage: " + submsg);
				if (submsg.startsWith("+DTMF:")) {            // Если ответ начинается с "+DTMF:" тогда:
					String symbol = submsg.substring(7, 8);     // Выдергиваем символ с 7 позиции длиной 1 (по 8)
					processingDTMF(symbol);                     // Логику выносим для удобства в отдельную функцию
				}
				else if (submsg.startsWith("+CLIP:")){         // При входящем звонке...
					if (submsg.indexOf(ACCESS_PHONE) > -1)
						sendATCommand("ATA", true);                 // ...отвечаем (поднимаем трубку)
					else
						sendATCommand("ATH", true);                 // ...не отвечаем (кладем трубку)
				}
				else if (submsg.length()<2)
					sendATCommand("AT", true);
				
				
			}
		} while (index > -1);                             // Пока индекс переноса строки действителен
	}
	if (Serial.available()) {                          // Ожидаем команды по Serial...
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


// Отдельная функция для обработки логики DTMF

void processingDTMF(String symbol) {
	Serial.println("Key: " + symbol);                   // Выводим в Serial для контроля, что ничего не потерялось

	                                 // Если 3 неудачных попытки, перестаем реагировать на нажатия
		if (symbol == "#") {
			if (dtmfCode == "123") { dtmfCode = "", launchEngine=true; }
			else if (dtmfCode =="456") { dtmfCode = "", openCar = true; }
			else if (dtmfCode == "789") { dtmfCode = "", closeCar = true; }
			 dtmfCode = "";                                 // После каждой решетки сбрасываем вводимую комбинацию
		}
		else {
			dtmfCode += symbol;                               // Все, что приходит, собираем в одну строку
		}
	
}
String sendATCommand(String cmd, bool waiting) {
	String _resp = "";                            // Переменная для хранения результата
	Serial.println(cmd);                          // Дублируем команду в монитор порта
	SIM800.println("AT"); delay(100);			//----------Костылсь от сна-------------
	SIM800.println(cmd);                          // Отправляем команду модулю
	if (waiting) {                                // Если необходимо дождаться ответа...
		_resp = waitResponse();                     // ... ждем, когда будет передан ответ
		// Если Echo Mode выключен (ATE0), то эти 3 строки можно закомментировать
		if (_resp.startsWith(cmd)) {                // Убираем из ответа дублирующуюся команду
			_resp = _resp.substring(_resp.indexOf("\r\n", cmd.length()) + 2);
		}
		Serial.println(_resp);                      // Дублируем ответ в монитор порта
	}
	return _resp;                                 // Возвращаем результат. Пусто, если проблема
}

String waitResponse() {                         // Функция ожидания ответа и возврата полученного результата
	String _resp = "";                            // Переменная для хранения результата
	uint32_t _starttimer = millis();             // Переменная для отслеживания таймаута (10 секунд)
	while (!SIM800.available() && (millis()-_starttimer) < 10000ul) {}; // Ждем ответа 10 секунд, если пришел ответ или наступил таймаут, то...
	if (SIM800.available()) {                     // Если есть, что считывать...
		_resp = SIM800.readString();                // ... считываем и запоминаем
	}
	else {                                        // Если пришел таймаут, то...
		Serial.println("Timeout...");               // ... оповещаем об этом и...
	}
	return _resp;                                 // ... возвращаем результат. Пусто, если проблема
}
//Temp & Battery check
void checkBatteryUT() {
	
	temp = DS18.requestTempAuto();
	vBat = VBatt();
	Serial.println("Engine temp: " + (String)temp);
	engineIsRunning = (vBat  > U1_RUNNING) ? true : false;
	//sendATCommand("AT", true);              // Проверка общего статуса
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
float VBatt() {      // блок замера напряжения с усреднением по выборке MEASURE_COUNT значений (избавляет от случайных скачков в замерах)
	long sumU1 = 0;       // переменные для суммирования кодов АЦП
	float utemp1 = 0;     // измеренное напряжение
	for (int timeCount = 0; timeCount < MEASURE_COUNT; timeCount++) sumU1 += analogRead(PORT_INPUT_BATTERY); // суммирование кодов АЦП
	utemp1 = sumU1 / MEASURE_COUNT * MEASURE_RATIO + DIODE_LOST;
	Serial.println("Battery voltage: "+(String)utemp1);
	return utemp1;
}

