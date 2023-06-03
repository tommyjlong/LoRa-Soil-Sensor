#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>  //Using version 4.6.0. Version 6.0.0 is too fat
#include <avr/wdt.h>
#include <avr/sleep.h>
#include "I2C_AHT10.h"
#include <ArduinoJson.h>

//#define NODENAME "LORA_POWER_1"
String node_id = String("ID") + "010280";
//String node_id = String("ID") + "010289";

//Set sleep time, when value is 1 almost sleep 20s,when value is 450, almost 1 hour.
#define SLEEP_CYCLE 450

//Lora set
//Set Lora frequency
// #define FREQUENCY 434.0
// #define FREQUENCY 868.0
#define FREQUENCY 915.0

#define BANDWIDTH 125.0
//#define SPREADING_FACTOR 9
#define SPREADING_FACTOR 7
//#define CODING_RATE 7
#define CODING_RATE 5
//#define OUTPUT_POWER 10
#define OUTPUT_POWER 17  //docs say range is 2 to 17 dBm
#define PREAMBLE_LEN 8
#define GAIN 0

//328p
#define DIO0 2
#define DIO1 6

#define LORA_RST 4
#define LORA_CS 10

#define SPI_MOSI 11
#define SPI_MISO 12
#define SPI_SCK 13

//pin set
#define VOLTAGE_PIN A3
#define PWM_OUT_PIN 9
#define SENSOR_POWER_PIN 5
#define ADC_PIN A2

#define DEBUG_OUT_ENABLE 1

SX1276 radio = new Module(LORA_CS, DIO0, LORA_RST, DIO1);
AHT10 humiditySensor;

String jsonoutput = "";  // string for JSON transmission

bool readSensorStatus = false;
int sensorValue = 0; // variable to store the value coming from the sensor
int batValue = 0;    // the voltage of battery
int count = 0;
int ADC_O_1;           // ADC Output First 8 bits
int ADC_O_2;           // ADC Output Next 2 bits
int16_t packetnum = 0; // packet counter, we increment per xmission
float temperature = 0.0;
float humidity = 0.0;

bool AHT_init()
{
    bool ret = false;
    Wire.begin();
    if (humiditySensor.begin() == false)
    {
#if DEBUG_OUT_ENABLE
        Serial.println("AHT10 not detected. Please check wiring. Freezing.");
#endif
    }

    if (humiditySensor.available() == true)
    {
        temperature = humiditySensor.getTemperature();
        humidity = humiditySensor.getHumidity();
        ret = true;
    }
    if (isnan(humidity) || isnan(temperature))
    {
#if DEBUG_OUT_ENABLE
        Serial.println(F("Failed to read from AHT sensor!"));
#endif
    }
    return ret;
}
void Lora_init()
{
    int state = radio.begin(FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE, SX127X_SYNC_WORD, OUTPUT_POWER, PREAMBLE_LEN, GAIN);
    if (state == ERR_NONE)
    {
#if DEBUG_OUT_ENABLE
        Serial.println(F("LoRa Radio Started successfully!"));
#endif
    }
    else
    {
#if DEBUG_OUT_ENABLE
        Serial.print(F("LoRa Radio failed, code: "));
        Serial.println(state);
#endif
        // while (true)
        //     ;
    }
}
void setup()
{
#if DEBUG_OUT_ENABLE
    Serial.begin(115200);
    Serial.println("Soil start.");
#endif
    delay(100);

    // set up Timer 1
    pinMode(PWM_OUT_PIN, OUTPUT);

    TCCR1A = bit(COM1A0);            // toggle OC1A on Compare Match
    TCCR1B = bit(WGM12) | bit(CS10); // CTC, scale to clock
    OCR1A = 1;

    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, HIGH);
    delay(100);

    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH); //Sensor power on
    delay(100);

    Lora_init();

    Wire.begin();
    if (humiditySensor.begin() == false)
    {

#if DEBUG_OUT_ENABLE
        Serial.println("AHT10 not detected. Please check wiring. Freezing.");
#endif
    }
#if DEBUG_OUT_ENABLE
    else
        Serial.println("AHT10 acknowledged.");
#endif

    do_some_work();
//setup over
#if DEBUG_OUT_ENABLE
    Serial.println("[Set]Sleep Mode Set");
#endif
    low_power_set();
}

void loop()
{
    wdt_disable();

    if (count > SLEEP_CYCLE) //(7+1) x 8S  450
    {
#if DEBUG_OUT_ENABLE
        //code start
        Serial.println("Code start>>");
#endif

        do_some_work();
        all_pins_low();

#if DEBUG_OUT_ENABLE
        //code end
        Serial.println("Code end<<");
#endif
        //count init
        count = 0;
    }

    low_power_set();
}

ISR(WDT_vect)
{
#if DEBUG_OUT_ENABLE
    Serial.print("[Watch dog]");
    Serial.println(count);
#endif
    delay(100);
    count++;
    //wdt_reset();
    wdt_disable(); // disable watchdog
}

//Set low power mode and into sleep
void low_power_set()
{
    all_pins_low();
    delay(10);
    // disable ADC
    ADCSRA = 0;

    sleep_enable();
    watchdog_init();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    delay(10);
    noInterrupts();
    sleep_enable();

    // turn off brown-out enable in software
    MCUCR = bit(BODS) | bit(BODSE);
    MCUCR = bit(BODS);
    interrupts();

    sleep_cpu();
    sleep_disable();
}

//Enable watch dog
void watchdog_init()
{
    // clear various "reset" flags
    MCUSR = 0;
    // allow changes, disable reset
    WDTCSR = bit(WDCE) | bit(WDE);
    WDTCSR = bit(WDIE) | bit(WDP3) | bit(WDP0); // set WDIE, and 8 seconds delay
    wdt_reset();                                // pat the dog
}

void do_some_work()
{

    digitalWrite(SENSOR_POWER_PIN, HIGH); // Sensor/RF95 power on
    digitalWrite(LORA_RST, HIGH);
    delay(5);
    pinMode(PWM_OUT_PIN, OUTPUT);    //digitalWrite(PWM_OUT_PIN, LOW);
    TCCR1A = bit(COM1A0);            // toggle OC1A on Compare Match
    TCCR1B = bit(WGM12) | bit(CS10); // CTC, scale to clock
    OCR1A = 1;                       // compare A register value (5000 * clock speed / 1024).When OCR1A == 1, PWM is 2MHz

    Lora_init();
    delay(50);

    //ADC2  AVCC as reference voltage
    ADMUX = _BV(REFS0) | _BV(MUX1);

    //ADC2  internal 1.1V as ADC reference voltage
    //ADMUX = _BV(REFS1) |_BV(REFS0) | _BV(MUX1);

    // 8  åˆ†é¢‘
    ADCSRA = _BV(ADEN) | _BV(ADPS1) | _BV(ADPS0);
    delay(50);
    for (int i = 0; i < 3; i++)
    {
        //start ADC conversion
        ADCSRA |= (1 << ADSC);

        delay(10);

        if ((ADCSRA & 0x40) == 0)
        {
            ADC_O_1 = ADCL;
            ADC_O_2 = ADCH;

            sensorValue = (ADC_O_2 << 8) + ADC_O_1;
            ADCSRA |= 0x40;
#if DEBUG_OUT_ENABLE
            Serial.print("ADC:");
            Serial.println(sensorValue);
#endif

            if (readSensorStatus == false)
                readSensorStatus = AHT_init();
        }
        ADCSRA |= (1 << ADIF); //reset as required
        delay(50);
    }

    //ADC3  internal 1.1V as ADC reference voltage
    ADMUX = _BV(REFS1) | _BV(REFS0) | _BV(MUX1) | _BV(MUX0);

    delay(50);
    for (int i = 0; i < 3; i++)
    {
        //start ADC conversion
        ADCSRA |= (1 << ADSC);

        delay(10);
        
        if ((ADCSRA & 0x40) == 0)
        {
            ADC_O_1 = ADCL;
            ADC_O_2 = ADCH;

            batValue = (ADC_O_2 << 8) + ADC_O_1;
            ADCSRA |= 0x40;
#if DEBUG_OUT_ENABLE
            Serial.print("BAT:");
            Serial.println(batValue);
            float bat = (float)batValue * 3.3;
            bat = bat / 1024.0;
            Serial.print(bat);
            Serial.println("V");
#endif
        }
        ADCSRA |= (1 << ADIF); //reset as required
        delay(50);
    }
    send_lora();
    delay(1000);
    radio.sleep();

    packetnum++;
    readSensorStatus = false;
    digitalWrite(SENSOR_POWER_PIN, LOW); // Sensor/RF95 power off
    delay(100);
}

void all_pins_low()
{
    pinMode(PWM_OUT_PIN, INPUT);
    pinMode(A4, INPUT_PULLUP);
    pinMode(A5, INPUT_PULLUP);

    delay(50);
}

void send_lora()
{
  //String message = "INDEX:" + (String)packetnum + " H:" + (String)humidity + " T:" + (String)temperature + " ADC:" + (String)sensorValue + " BAT:" + (String)batValue;
  //String back_str = node_id + " REPLY : SOIL " + message;


    // create battery_level instead of raw ADC value   
      float battery_level = (float)batValue * 3.3;
      battery_level = battery_level / 1024.0;
    
    // Moisture ADC value decreases as battery_level decreases, so make a rough adjustment
#define BATT_ADJUST_FACTOR 45  
    sensorValue = sensorValue -((battery_level - 2.0)*BATT_ADJUST_FACTOR);
    if (sensorValue < 500)
        sensorValue = 500;
     
    // Modify SensorValue to show moisture between 0 to 100 percent 
    // instead of dryness ADC levels of 1000 = Air dry to 500 = ultrawet 
    sensorValue = 100 -((sensorValue - 500)/5);
          
    // Create json object for transfer
    DynamicJsonDocument root(256);
    root["node_id"] = node_id;
  //root["hum"] = (String)humidity;
  //root["temp"] = (String)temperature;
  //root["adc"] = (String)sensorValue;
  //root["bat"] = (String)battery_level;
    root["hum"] = humidity;
    root["temp"] = temperature;
    root["adc"] = sensorValue;
    root["bat"] = battery_level;

  serializeJson(root, jsonoutput);

#if DEBUG_OUT_ENABLE
  Serial.print("LoRa TX: ");
  //Serial.println(back_str);
  serializeJson(root, Serial);
  Serial.println("");
#endif

  //radio.transmit(back_str);
    radio.transmit(jsonoutput);
    jsonoutput = "";
}
