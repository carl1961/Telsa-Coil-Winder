#include <TM1637Display.h>                                           // https://github.com/avishorp/TM1637
#include <ClickEncoder.h>                                            // https://github.com/0xPIT/encoder/tree/arduino
#include <TimerOne.h>                                                // https://playground.arduino.cc/Code/Timer1/
#include <EEPROM.h>

#define numberOfcentiSeconds( _time_ ) (( _time_ / 10 ) % 100 )       // amount of centiseconds
#define numberOfSeconds( _time_ ) (( _time_ / 1000 ) % 60 )           // amount of seconds
#define numberOfMinutes( _time_ ) ((( _time_ / 1000 ) / 60 ) % 60 )   // amount of minutes 
#define numberOfHours( _time_ ) (( _time_ / 1000 ) / 3600 )           // amount of hours

#define config_version "v1"
#define config_start 16

//#define DEBUG

#ifdef DEBUG
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x) 
#endif

const int enc_pin_A = 4;                                              // rotary encoder first data pin A at D5 pin
const int enc_pin_B = 5;                                              // rotary encoder second data pin B at D4 pin, if the encoder moved in the wrong direction, swap A and B
const int enc_pin_SW = 7;                                             // rotary encoder switch button at D7 pin
const int rpm_pin = 9;                                                // RPM output to step motor at D9 pin  
const int rpm_en_pin = 8;                                             // step motor driver enable at D8 pin 
const int dir_pin = 6;                                                // step motor driver rotation direction at D6 pin  

bool rpmset = true;                                                  // no rpm menu at start
bool colon = true;                                                    // timer colon active at start
bool done = true;                                                     
int RPM, lastRPM, last_tempRPM, tempRPM, timerHours, timerMinutes, timerSeconds;
int16_t value, lastValue;
unsigned long colon_ms, timeLimit, timeRemaining, lastAccelTime, lastDecelTime, lastRPMTime, savemillis, himillis;

uint8_t save[] = {
  SEG_A|SEG_F|SEG_G|SEG_C|SEG_D,                                      // S
  SEG_A|SEG_B|SEG_F|SEG_G|SEG_E|SEG_C,                                // A
  SEG_F|SEG_E|SEG_C|SEG_D|SEG_B,                                      // V
  SEG_A|SEG_F|SEG_D|SEG_G|SEG_E,                                      // E
};

uint8_t hi[] = {
  0x00, 0x00,
  SEG_F|SEG_B|SEG_G|SEG_C|SEG_E,                                      // H
  SEG_B|SEG_C,                                                        // I
};


TM1637Display display( 2, 3 );                                        // TM1637 CLK connected to D2 and DIO to D3

ClickEncoder *encoder;



void timerIsr() {                                                     // encoder interupt service routine
  
  encoder -> service();  
  
}

typedef struct {                                                       // settings to save in eeprom
    char version[3];
    int timerHours;
    int timerMinutes;
    int RPM;
    
} settings;


settings cfg = {
     config_version,
     0,                                                                // int timerHours  
     15,                                                               // int timerMinutes
     20                                                                // int RPM
};



bool loadConfig() {                                                   

  if (EEPROM.read( config_start + 0 ) == config_version[0] &&
      EEPROM.read( config_start + 1 ) == config_version[1] ){

    for (int i = 0; i < sizeof( cfg ); i++ ){
      *(( char* )&cfg + i) = EEPROM.read( config_start + i );
    }
    DEBUG_PRINTLN( "configuration loaded:" );
    DEBUG_PRINTLN( cfg.version );

    timerHours = cfg.timerHours;
    timerMinutes = cfg.timerMinutes;
    RPM = cfg.RPM;
    return true;

  }
  return false;

}



void saveConfig() {
  for ( int i = 0; i < sizeof( cfg ); i++ )
    EEPROM.write( config_start + i, *(( char* )&cfg + i ));
    DEBUG_PRINTLN( "configuration saved" );
}



void setup() {
  
  Serial.begin( 115200 );                                             // serial for debug

  display.setBrightness( 0x04 );                                      // medium brightness, to reduce LM1117-5V current and overheating, maximum is 7
 
  encoder = new ClickEncoder( enc_pin_A, enc_pin_B, enc_pin_SW, 4 );  // rotary encoder init, last value is encoder steps per notch
  
  Timer1.initialize( 1000 );                                          // set the timer period in us, 1ms
  Timer1.attachInterrupt( timerIsr );                                 // attach the service routine
  
  pinMode( rpm_pin, OUTPUT );   
  pinMode( rpm_en_pin, OUTPUT ); 
  digitalWrite(rpm_en_pin, HIGH);          
  pinMode( dir_pin, OUTPUT );  
  digitalWrite(dir_pin, HIGH);                               
  lastValue = 0;   
  lastAccelTime = 0;
  lastDecelTime = 0;
  colon_ms = millis();
  savemillis = -2000;
  himillis = 0;
  if ( !loadConfig() ) {                                              // checking and loading configuration
    DEBUG_PRINTLN( "configuration not loaded!" );
    saveConfig();                                                     // default values if no config
  }

}



void menuTimer() {
  
   unsigned long runTime;
   timeLimit = 0;
  
   while  ( done && !rpmset )  {
      value += encoder -> getValue();
            if ( value > lastValue ) {
              timerMinutes++;                                         // one rotary step is 1 minute
              if ( timerHours >= 12 && timerMinutes >= 0 ) {
                timerHours = 12;                                      // max 12 hours
                timerMinutes = 0;
              }
              if ( timerMinutes >= 60 ) {
                timerHours++;
                timerMinutes = 0;
              }   
            } 
            else if ( value < lastValue ) {
              if ( timerMinutes > 0) timerMinutes--;      
              else if ( timerMinutes == 0 && timerHours > 0 ) {
                timerHours--;
                timerMinutes = 59;
              } 
            }
            if ( value != lastValue ) {
              lastValue = value;
              DEBUG_PRINT( "Encoder value: " );
              DEBUG_PRINTLN( value );
            }
            
    if ( millis() - himillis < 2000 )                                 // say HI at power on                          
      display.setSegments( hi, 4, 0 );  
      
    else if ( millis() - savemillis < 2000 )                          // show SAVE if saving config to eeprom                          
      display.setSegments( save, 4, 0 );
      
    else                                                              // display time to countdown, leading zeros active if no hours, colon active
      display.showNumberDecEx( timeToInteger( timerHours, timerMinutes ), 0x80 >> true , timerHours == 0 );
    
    buttonCheck();                                                    // check if rotary encoder button pressed
    rpmCheck();
	
  }
   
  runTime = millis();                                                 // 1000 ms = 1s, so 1 minute is 60000 ms, and 1 hour is 3600000 ms
  timeLimit = timerHours * 3600000 + timerMinutes * 60000 + runTime;  // add the runtime until timer starts to timeLimit, limit is compared with mcu millis in main loop
   
}



void menuRPM()  {

    value += encoder -> getValue();
    
    if ( value > lastValue ) {
      if ( RPM >= 50 )
        RPM = 50;                                                     // max rpm is 50 cw
      else if (( RPM >=-2) && ( RPM <=0)) {
        RPM  = 2;                                                     // min rpm is 2
      }
      else
        RPM +=1;                                                      // one rotary step is 1 rpm  
      }
          
    else if ( value < lastValue ) {
      if ( RPM <= -50 )
        RPM = -50;                                                    // max rpm is -50 ccw
      else if (( RPM <=2) && ( RPM >=0)) {
        RPM = -2;
      }
      else
        RPM -=1;                                                      // one rotary step is 1 rpm
    }
    
    if ( value != lastValue ) {
      lastRPMTime = millis(); 
      lastValue = value;
      DEBUG_PRINT( "RPM value: " );
      DEBUG_PRINTLN( RPM );
      lastRPM=RPM;
    }                                               

    if ( millis() - lastRPMTime > 20000 )  {                          // after 20s inactivity in rmp set menu, comeback to countdown
       rpmset = false;
       done = false;
       DEBUG_PRINTLN( "Back to countdown" );

    }
  
  
  display.showNumberDecEx( RPM, 0x80 >> false , false );              // show rpm speed, no colon, no leading zeros
  
  buttonCheck();                                                      // check rotary encoder button
  timeCheck();                                                        // check timer if finished
  rpmCheck();
  
}



void countdown() {

  int n_centisec = numberOfcentiSeconds( timeRemaining );             // amount of centiseconds in remaining time
  int n_seconds = numberOfSeconds( timeRemaining );                   // amount of seconds in remaining time
  int n_minutes = numberOfMinutes( timeRemaining );                   // amount of minutes in remaining time 
  int n_hours = numberOfHours( timeRemaining );                       // amount of hours in remaining time
  
   if (( millis() - colon_ms ) >= 500 ) {                             // colon is blinking with about 0.5s period
        colon_ms = millis();
        colon =! colon;
        if ( colon ) {                                                // print timer countdown with about 1s period
          if ( n_hours )  {
            DEBUG_PRINT( n_hours );
            DEBUG_PRINT( " Hours " );
          }
          if ( n_minutes )  {
           DEBUG_PRINT( n_minutes );
            DEBUG_PRINT( " Minutes " );
          }  
          DEBUG_PRINT( n_seconds );
          DEBUG_PRINTLN( " Seconds" );
          DEBUG_PRINT( RPM );
          DEBUG_PRINTLN( " RPM" );
        }
   }
   if ( !n_hours ) {                                                  
      if ( n_minutes )  {                                             // show minutes and seconds if no hours left
        n_hours = n_minutes;
        n_minutes = n_seconds;
      }
      else  {                                                         // show seconds and centiseconds if no minutes left
        n_hours = n_seconds;
        n_minutes = n_centisec;
    }
   }
                                                                      // show time, hours in first two positions, with colon and leading zeros enabled 
   display.showNumberDecEx( timeToInteger( n_hours, n_minutes ), 0x80 >> colon, n_hours == 0 );

   buttonCheck();                                                     // check rotary encoder button
   timeCheck();                                                       // check timer if finished
   rpmCheck();
   
}

void rpmCheck()  {

  if (!done || rpmset) {
    int tempRPM = last_tempRPM;  

      if ( tempRPM < RPM ) {                                              // acceleration 
        if ( millis() - lastAccelTime > 200 ) {
          lastAccelTime = millis();
          tempRPM++;
        }
      }
      else if ( tempRPM > RPM) {
        if ( millis() - lastDecelTime > 200 ) {                           // dececeleration
          lastDecelTime = millis();
          tempRPM--;
        }
      }
    if ( last_tempRPM != tempRPM ){
        DEBUG_PRINT( tempRPM );
        DEBUG_PRINTLN( " TEMPRPM" );
    }
  
    last_tempRPM = tempRPM;
  
    if ( last_tempRPM >= 0 )
      digitalWrite( dir_pin, HIGH );
    else
      digitalWrite( dir_pin, LOW );

     if ( abs(last_tempRPM) <= 2 )                                        // min rpm value is 2 or -2
      Timer1.pwm( rpm_pin, 512, 9375);                                    // 9375 = 18750 / 2
     else                                       // one motor revolution is 200 steps, with driver factor 1/16 per step is 3200, 
      Timer1.pwm( rpm_pin, 512, (int) 18750 / abs(last_tempRPM)   ); 
  }                                             // so for one revolution per minute is 60 000 000 / 3200 = 18750 period, 512 is duty 50%
  
  else
    if ( last_tempRPM ==0 ) {                                             // default 20 rpm if done, to prevent error readings from encoder if rpm = 0
      Timer1.pwm( rpm_pin, 512, 750);
  }
  
																	
 
}

int timeToInteger( int _hours, int _minutes ) {
  
  int result = 0;
  result += _hours * 100;
  result += _minutes;
  
  return result;
  
}


void buttonCheck() {
  
 ClickEncoder::Button b = encoder -> getButton();
   if ( b != ClickEncoder::Open ) {
      DEBUG_PRINT( "Button: " );
      
      #define VERBOSECASE( label ) case label: DEBUG_PRINTLN( #label ); break;
      
      switch ( b ) {
         VERBOSECASE( ClickEncoder::Pressed );
         VERBOSECASE( ClickEncoder::Released )
         
       case ClickEncoder::Clicked:
         DEBUG_PRINTLN( "ClickEncoder::Clicked" );
         if ( !isTimerFinished() )  {                                 // can't set rpm or start countdown if timer not set (00:00)
            if ( !rpmset )  {                                         // set rpm
				        rpmset = true;
                digitalWrite(rpm_en_pin, LOW);
                value = encoder -> getValue();
                lastValue = value;  
                DEBUG_PRINTLN( "RPM set" );
            }
            else {                                                    // start or go back to countdown if rpm set 
              done = false;
              rpmset = false;
              DEBUG_PRINTLN( "Countdown" );
            } 
         }
       break;
                
       case ClickEncoder::Held:                                       // timer reset if rotary encoder button held for about 2s
         DEBUG_PRINTLN( "ClickEncoder::Held" );
         if ( !done ) timerFinished();
       break;

        case ClickEncoder::DoubleClicked:                             // save config if rotary encoder button double clicked
         DEBUG_PRINTLN( "ClickEncoder::DoubleClicked" );
          if ( done && !isTimerFinished() ) {
            cfg.timerHours = timerHours;
            cfg.timerMinutes = timerMinutes;
            cfg.RPM = RPM;
            saveConfig();
            savemillis = millis();                                    // time marker used to show SAVE on display - menuTimer()
          }
       break;

      } 
   }
}



void timeCheck() {
  
  timeRemaining = timeLimit - millis();                               // calculate time remaining
    
  if ( timeRemaining < 500 ) timerFinished();                         // timer reset if coundown finished

}



bool isTimerFinished() {
  
  return timerHours == 0 && timerMinutes == 0 && timerSeconds == 0;  
  
}



void timerFinished()  {
  
  timerHours = 0;                                                     // timer reset, stop motor
  timerMinutes = 0;
  timerSeconds = 0; 
  last_tempRPM = 0;
  value = encoder -> getValue();
  lastValue = value;                                                  // set last encoder value
  rpmset = false;
  done = true;
  digitalWrite( rpm_en_pin, HIGH );
  DEBUG_PRINTLN( "Timer finished" );
  
}



void loop() {
  
  if ( !rpmset ) {
    if ( done ) menuTimer();
    else countdown();
    lastRPMTime = millis();
  }
  else
    menuRPM();

}
