//------------------------------------------------------------------------------------------------------------------------------------------------
// emonGLCD Solar PV monitor example - compatiable with Arduino 1.0 

// emonGLCD documentation http://openEnergyMonitor.org/emon/emonglcd
// solar PV monitor build documentation: http://openenergymonitor.org/emon/applications/solarpv

// For use with emonTx setup with 2CT with CT 1 monitoring consumption/grid and CT 2 monitoring PV generation .
// The CT's should be clipped on with the orientation so grid reading is postive when importing and negative when exporting. Generation reading should always be positive. 

// Correct time is updated via NanodeRF which gets time from internet, this is used to reset Kwh/d counters at midnight. 

// Temperature recorded on the emonglcd is also sent to the NanodeRF for online graphing

// this sketch is currently setup for type 1 solar PV monitoring where CT's monitor generation and consumption separately
// The sketch assumes emonx.power1 is consuming/grid power and emontx.power4 is solarPV generation
// to use this sketch for type 2 solar PV monitoring where CT's monitor consumption and grid import/export using an AC-AC adapter to detect current flow direction 
// change type to '2' on emonGLCD setup section below.    

// GLCD library by Jean-Claude Wippler: JeeLabs.org
// 2010-05-28 <jcw@equi4.com> http://opensource.org/licenses/mit-license.php
//
// History page by vworp https://github.com/vworp
//
// Authors: Glyn Hudson and Trystan Lea
// Part of the: openenergymonitor.org project
// Licenced under GNU GPL V3
// http://openenergymonitor.org/emon/license

// Libraries in the standard arduino libraries folder:
//
//	- OneWire library	http://www.pjrc.com/teensy/td_libs_OneWire.html
//	- DallasTemperature	http://download.milesburton.com/Arduino/MaximTemperature
//                           or https://github.com/milesburton/Arduino-Temperature-Control-Library
//	- JeeLib		https://github.com/jcw/jeelib
//	- RTClib		https://github.com/jcw/rtclib
//	- GLCD_ST7565		https://github.com/jcw/glcdlib
//
// Other files in project directory (should appear in the arduino tabs above)
//	- icons.ino
//	- templates.ino
//
//-------------------------------------------------------------------------------------------------------------------------------------------------

#define RF69_COMPAT 1 // set to 1 to use RFM69CW 
#include <JeeLib.h>   // make sure V12 (latest) is used if using RFM69CW

//#include <EEPROMex.h>
#include <EEPROM.h>
#include <JeeLib.h>
#include <GLCD_ST7565.h>
#include <avr/pgmspace.h>
GLCD_ST7565 glcd;

#include <OneWire.h>		    // http://www.pjrc.com/teensy/td_libs_OneWire.html
#include <DallasTemperature.h>      // http://download.milesburton.com/Arduino/MaximTemperature/ (3.7.2 Beta needed for Arduino 1.0)
#include <RTClib.h>                 // Real time clock (RTC) - used for software RTC to reset kWh counters at midnight
#include <Wire.h>                   // Part of Arduino libraries - needed for RTClib
RTC_Millis RTC;

//--------------------------------------------------------------------------------------------
// RFM12B Settings
//--------------------------------------------------------------------------------------------
#define MYNODE 20            // Should be unique on network, node ID 30 reserved for base station
#define RF_freq RF12_433MHZ     // frequency - match to same frequency as RFM12B module (change to 868Mhz or 915Mhz if appropriate)
#define group 210            // network group, must be same as emonTx and emonBase

//---------------------------------------------------
// Data structures for transfering data between units
//---------------------------------------------------
typedef struct { int power1, power2, power3, power4, Vrms, temp, rssi; } PayloadTX;         // neat way of packaging data for RF comms
PayloadTX emontx;

typedef struct { int temperature; } PayloadGLCD;
PayloadGLCD emonglcd;

//---------------------------------------------------
// emonGLCD SETUP
//---------------------------------------------------
//#define emonGLCDV1.3               // un-comment if using older V1.3 emonGLCD PCB - enables required internal pull up resistors. Not needed for V1.4 onwards 
const int SolarPV_type=2;            // Select solar PV wiring type - Type 1 is when use and gen can be monitored seperatly. Type 2 is when gen and use can only be monitored together, see solar PV application documentation for more info
const int maxgen=4200;              // peak output of soalr PV system in W - used to calculate when to change cloud icon to a sun
const int PV_gen_offset=20;         // When generation drops below this level generation will be set to zero - used to force generation level to zero at night

const int greenLED=6;               // Green tri-color LED
const int redLED=9;                 // Red tri-color LED
const int LDRpin=4;    		    // analog pin of onboard lightsensor 
const int switch1=15;               // Push switch digital pins (active low for V1.3, active high for V1.4)
const int switch2=16;
const int switch3=19;

//---------------------------------------------------
// emonGLCD variables 
//---------------------------------------------------
int hour = 12, minute = 0;
double usekwh = 0, genkwh = 0;
#define N_POWER_POINTS 25
double use_history[7], gen_history[7];
int power_history[N_POWER_POINTS];
//byte power_history2[128];
int cval_use, cval_gen;
byte page = 1;


//---------------------------------------------------
// Temperature Sensor Setup
//---------------------------------------------------
#define ONE_WIRE_BUS 5              // temperature sensor connection - hard wired 
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
double temp, maxtemp,mintemp;


//-------------------------------------------------------------------------------------------- 
// Flow control
//-------------------------------------------------------------------------------------------- 
unsigned long last_emontx;                   // Used to count time from last emontx update
unsigned long last_emonbase;                   // Used to count time from last emontx update
boolean last_switch1_state, switch1_state, last_switch2_state, switch2_state; 
unsigned long fast_update, slow_update, blink_pace; 
int count_power_average = 0;
long power_total = 0;
boolean blink_led = false; 
//--------------------------------------------------------------------------------------------
// Setup
//--------------------------------------------------------------------------------------------
void setup()
{
  //setup  values for testing EEPROM read
  //for (int i=0; i<12 ; i++) EEPROM_write(i,i*10);
  /*
  EEPROM_writeDouble( 0 , 13.1);
  EEPROM_writeDouble( 4 ,  7.9);
  EEPROM_writeDouble( 8 , 10.0);
  EEPROM_writeDouble(12 , 10.1);
  EEPROM_writeDouble(16 , 21.5);
  EEPROM_writeDouble(20 , 26.9);
  EEPROM_writeDouble(24 , 17.7);
  EEPROM_writeDouble(28 , 18.3);
  EEPROM_writeDouble(32 , 20.0);
  EEPROM_writeDouble(36 ,  5.2);
  EEPROM_writeDouble(40 ,  1.9);
  EEPROM_writeDouble(44 , 11.1);
  */
 //  for(int i=0; i<N_POWER_POINTS; i++) {
 //    power_history[i] = N_POWER_POINTS*i-i*i;
 //  }
  Serial.begin(9600);                      // set up Serial library at 9600 bps
  delay(500);                   	   //wait for power to settle before firing up the RF
  rf12_initialize(MYNODE, RF_freq,group);
  delay(100);				   //wait for RF to settle befor turning on display
  glcd.begin(0x19);
  glcd.backLight(200);
  
  sensors.begin();                         // start up the DS18B20 temp sensor onboard  
  //delay(5000); 				   //wait for power to settle before reading temp
  //Serial.println(sensors.getDeviceCount());
  sensors.requestTemperatures();
  temp = (sensors.getTempCByIndex(0));     // get inital temperture reading
  mintemp = temp; maxtemp = temp;          // reset min and max

  pinMode(greenLED, OUTPUT); 
  pinMode(redLED, OUTPUT); 
  
  #ifdef emonGLCDV1.3                      //enable internal pull up resistors for push switches on emonGLCD V1.3 (old) 
  pinMode(switch1, INPUT); pinMode(switch2, INPUT); pinMode(switch2, INPUT);
  digitalWrite(switch1, HIGH); digitalWrite(switch2, HIGH); digitalWrite(switch3, HIGH); 
  #endif
  
  //restore EEPROM values
  int i;
  for (i=0; i<6; i++) {
    if (EEPROM_readDouble(i*4) != 255) { 
      use_history[i+1] = (double) EEPROM_readDouble(i*4);
    }
    else
    {
      use_history[i+1] = 0;
    }
  }
  for (i=0; i<6; i++){
    if (EEPROM_readDouble((i+6)*4) != 255) { 
      gen_history[i+1] = (double) EEPROM_readDouble((i+6)*4);
    }
    else
    {
      gen_history[i+1] = 0;
    }
    
  }
  for(int i=0; i<N_POWER_POINTS; i++) {
    power_history[i] = -1;
  }
  //for(int i=0; i<128; i++) {
  //  power_history2[i] = 0;
  //}

}
//--------------------------------------------------------------------------------------------
// Loop
//--------------------------------------------------------------------------------------------
void loop()
{
   //for (int i=0; i<6;i++) Serial.println(gen_history[i]);
   //for (int i=0; i<6;i++) Serial.println(use_history[i]);
  //Serial.print("freeram = "); 
  //Serial.println(freeRam());
 

  if (rf12_recvDone())
  {
    if (rf12_crc == 0 && (rf12_hdr & RF12_HDR_CTL) == 0)  // and no rf errors
    {
      int node_id = (rf12_hdr & 0x1F);
      //Serial.print("nodeid = ");
      //Serial.println(node_id);
      if (node_id == 10) {emontx = *(PayloadTX*) rf12_data; last_emontx = millis();}
      
      if (node_id == 15)
      {
        RTC.adjust(DateTime(2013, 1, 1, rf12_data[1], rf12_data[2], rf12_data[3]));
        last_emonbase = millis();
      } 
    }
  }

  //--------------------------------------------------------------------------------------------
  // Display update every 200ms
  //--------------------------------------------------------------------------------------------
  if ((millis()-fast_update)>200)
  {
    fast_update = millis();
    
    DateTime now = RTC.now();
    int last_hour = hour;
    hour = now.hour();
    minute = now.minute();
    
    if (SolarPV_type==1){
    usekwh += (emontx.power1 * 0.2) / 3600000;
    genkwh += (emontx.power4 * 0.2) / 3600000;
    }
    
    if (SolarPV_type==2){
    usekwh += ((emontx.power1 + emontx.power4) * 0.2) / 3600000;
    genkwh += (emontx.power4 * 0.2) / 3600000;
    }
    
    if (hour>=6 && hour <= 18) {
      power_total += emontx.power4;
      count_power_average++;
      //Serial.println(emontx.power4);
      //Serial.println(power_total);
      //Serial.println(count_power_average);
    }
    
    if (last_hour == 23 && hour == 00) 
    { 
      for (int i=6; i>0; i--) {
        gen_history[i] = gen_history[i-1];
        EEPROM_writeDouble((i+6-1)*4 , gen_history[i-1]);
      } 
      genkwh = 0;
      for(int i=6; i>0; i--) {
        use_history[i] = use_history[i-1];
        EEPROM_writeDouble((i-1)*4 , use_history[i-1]);
      }
      for(int i=0; i<N_POWER_POINTS; i++) {
        power_history[i] = -1;
      }
      usekwh = 0;

      
    }
    gen_history[0] = genkwh;
    use_history[0] = usekwh;
    
    if (SolarPV_type==1){
    cval_use = cval_use + (emontx.power1 - cval_use)*0.50;
    cval_gen = cval_gen + (emontx.power4 - cval_gen)*0.50;
    }
    
    if (SolarPV_type==2){
    cval_use = cval_use + ((emontx.power1+emontx.power4) - cval_use)*0.50;
    cval_gen = cval_gen + (emontx.power4 - cval_gen)*0.50;
    /*Serial.println(emontx.power1);
    Serial.println(emontx.power2);
    Serial.println(emontx.power3);
    Serial.println(emontx.power4);
    Serial.println(emontx.Vrms);
    Serial.println(emontx.temp);
    Serial.println(emontx.rssi);*/
    }
      
    if (cval_gen<PV_gen_offset) cval_gen=0;                  //set generation to zero when generation level drops below a certian level (at night) eg. 20W
    
    last_switch1_state = switch1_state;
    switch1_state = digitalRead(switch1);  
    if (!last_switch1_state && switch1_state) { page += 1; if (page>3) page = 1; }

    switch (page)
    {
    case 1:
      //Serial.println("page1");
      draw_solar_page(cval_use, usekwh, cval_gen, maxgen, genkwh, temp, mintemp, maxtemp, hour,minute, last_emontx, last_emonbase);
      break;
    /*case 2:
      draw_power_page( "POWER" ,cval_use, "USE", usekwh);
      draw_temperature_time_footer(temp, mintemp, maxtemp, hour,minute);
      glcd.refresh();
    else if (page==3)
    {
      draw_power_page( "SOLAR" ,cval_gen, "GEN", genkwh);
      draw_temperature_time_footer(temp, mintemp, maxtemp, hour,minute);
      glcd.refresh();
    }*/
    case 2:
      //Serial.println("page2");
      draw_history_page(gen_history, use_history);   
      break;
    case 3:
      //Serial.println("page3");
      draw_power_history_page(power_history);   
      break;
    }  
    
    int LDR = analogRead(LDRpin);                     // Read the LDR Value so we can work out the light level in the room.
    int LDRbacklight = map(LDR, 0, 1023, 50, 250);    // Map the data from the LDR from 0-1023 (Max seen 1000) to var GLCDbrightness min/max
    LDRbacklight = constrain(LDRbacklight, 0, 255);   // Constrain the value to make sure its a PWM value 0-255
    //LDRbacklight = 0;
    /*Serial.print("LDR = ");
    Serial.println(LDR);
    Serial.print("LDRbacklight = ");
    Serial.println(LDRbacklight);*/
    
    //if (millis()-backlight_time>10000) {
    if ((hour > 22) ||  (hour < 5)) glcd.backLight(0);
    //else if (!digitalRead(switch2)) glcd.backLight(LDRbacklight);
    else glcd.backLight(LDRbacklight);  
      
    //last_switch2_state = switch2_state;
    //switch2_state = digitalRead(switch2);  
    //if (!last_switch2_state && switch2_state) { glcd.backLight(LDRbacklight);}

    int PWRleds= map(cval_use-cval_gen, 0, maxgen, 0, 255);     // Map importing value from (LED brightness - cval3 is the smoothed grid value - see display above 
    if (PWRleds<0) PWRleds = PWRleds*-1;                        // keep it positive 
    PWRleds = constrain(PWRleds, 0, 255);                       // Constrain the value to make sure its a PWM value 0-255
   
    if (cval_gen>PV_gen_offset) {
      if (cval_gen > cval_use) {            //show green LED when gen>consumption cval are the smooth curve values  
	analogWrite(redLED, 0);         
	analogWrite(greenLED, PWRleds);    
        
      } else {                              //red if consumption>gen
        analogWrite(greenLED, 0); 
	analogWrite(redLED, PWRleds);   
      }
    } else {                                //Led's off at night and when solar PV is not generating
      analogWrite(redLED, 0);
      analogWrite(greenLED, 0);
    }
  } 
  
  if (cval_use-cval_gen>4500) {             //if USAGE is more than allowed by energy supplier
    if ((millis()-blink_pace)>500) {        //4500 is the average max, 5000 allowed (turn on both LEDs and display max light)
                                            //for small period. Above 5000 will open the circuit (blink LEDs and display).
      if (cval_use-cval_gen<=5000) {        
        analogWrite(redLED, 255);   
        analogWrite(greenLED, 255);   
        glcd.backLight(255);
      } else if (cval_use-cval_gen>5000) {
        analogWrite(redLED, 255*blink_led);   
        analogWrite(greenLED, 255*(blink_led));   
        glcd.backLight(255*blink_led);  
        blink_led=!blink_led;
      }
    blink_pace=millis();
    }  
  }
  
  if ((millis()-slow_update)>10000)
  {
    slow_update = millis();

    sensors.requestTemperatures();
    double rawtemp = (sensors.getTempCByIndex(0));
    //Serial.print("temp = ");
    //Serial.println(rawtemp);
    if ((rawtemp>-20) && (rawtemp<50)) temp=rawtemp;                  //is temperature withing reasonable limits?
    if (temp > maxtemp) maxtemp = temp;
    if (temp < mintemp) mintemp = temp;
   
    emonglcd.temperature = (int) (temp * 100);                          // set emonglcd payload
    rf12_sendNow(0, &emonglcd, sizeof emonglcd);                     //send temperature data via RFM12B using new rf12_sendNow wrapper -glynhudson
    rf12_sendWait(2);    
  }
  
// ************************************************
// Power Graph values
// ************************************************
  if (hour>=6 && hour <= 18) {
    if (minute % 30 == 0) {     //every 30minutes store a reading of the power for the plot.     
      Serial.print(hour);
      Serial.println(minute);
      int pos = (hour-6)*2 + minute/30; //calculate in-array position
      Serial.println(pos);
      Serial.println(power_history[pos]);
      if (power_history[pos] < 0) {
        power_history[pos] = power_total/count_power_average;
        power_total = 0;
        count_power_average = 0;
      }
    }
  }
}


// ************************************************
// Write floating point values to EEPROM
// ************************************************
void EEPROM_writeDouble(int address, double value)
{
   byte* p = (byte*)(void*)&value;
   for (int i = 0; i < sizeof(value); i++)
   {
      EEPROM.write(address++, *p++);
   }
}
 
// ************************************************
// Read floating point values from EEPROM
// ************************************************
double EEPROM_readDouble(int address)
{
   double value = 0.0;
   byte* p = (byte*)(void*)&value;
   for (int i = 0; i < sizeof(value); i++)
   {
      *p++ = EEPROM.read(address++);
   }
   return value;
}

int freeRam() 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
