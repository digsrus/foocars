#include <Arduino.h>
#include <Wire.h>

#include "MPU9250.h"
//#include <SoftPWMServo.h>
#include <Servo.h>

#define DEBUG_SERIAL 1

#define MAX_CMD_BUF 17 
#define CMD_AUTO 0
#define CMD_STR 1
#define CMD_THR 2
#define CMD_TIME 3

#define CENTERED_STEERING_VALUE 1500
#define CENTERED_THROTTLE_VALUE 1500

#define MINIMUM_STEERING_VALUE 1100
#define MAXIMUM_STEERING_VALUE 1700
#define MINIMUM_THROTTLE_VALUE 1250
#define MAXIMUM_THROTTLE_VALUE 1650

#define THROTTLE_THRESHOLD_TO_SHUTDOWN_AUTO 1600
    
enum commandEnumeration{
    NOT_ACTUAL_COMMAND = 0,
    RC_SIGNAL_WAS_LOST = 1,
    RC_SIGNALED_STOP_AUTONOMOUS = 2,
    STEERING_VALUE_OUT_OF_RANGE = 3,
    THROTTLE_VALUE_OUT_OF_RANGE= 4,
    RUN_AUTONOMOUSLY = 5,
    STOP_AUTONOMOUS = 6,
    STOPPED_AUTO_COMMAND_RECEIVED = 7,
    NO_COMMAND_AVAILABLE = 8,
    GOOD_PI_COMMAND_RECEIVED = 9,
    TOO_MANY_VALUES_IN_COMMAND = 10,
    GOOD_RC_SIGNALS_RECEIVED = 11
};

struct commandDataStruct {
int command;
int16_t ax;        // acceleration
int16_t ay;
int16_t az;
int16_t gx;        // yaw
int16_t gy;        // pitch
int16_t gz;        // roll
unsigned long time;    // millis
int str;        // steering 1000-2000
int thr;        // throttle 1000-2000
// int checksum;    someday???
};

const int PIN_STR = 9;
const int PIN_THR = 7;

byte LEDdebugPins[] = {24, 25, 26, 27, A14, A13, A12, A11};    // lsb on the right

int gTotalNumberOfPassesForCommandDisplay = 10000;
int gCountOfPassesForCommandDisplay = gTotalNumberOfPassesForCommandDisplay;

boolean gWantsLEDon;

boolean gIsInAutonomousMode;
int gTheOldRCcommand;
int gTheOldPiCommand;

Servo ServoSTR;
Servo ServoTHR;

//imu unit object
MPU9250 ottoIMU;

/*
    Define IMU mpu9250 values
*/
#define        MPU9250_ADDRESS            0x68
#define        MAG_ADDRESS            0x0C

#define        GYRO_FULL_SCALE_250_DPS        0x00    
#define        GYRO_FULL_SCALE_500_DPS        0x08
#define        GYRO_FULL_SCALE_1000_DPS    0x10
#define        GYRO_FULL_SCALE_2000_DPS    0x18

#define        ACC_FULL_SCALE_2_G        0x00    
#define        ACC_FULL_SCALE_4_G        0x08
#define        ACC_FULL_SCALE_8_G        0x10
#define        ACC_FULL_SCALE_16_G        0x18

#define WHO_AM_I_MPU9250 0x75 // Should return 0x71

//These lines are for the input capture for pwm read off RC
#define RC_INPUT_STR 3
#define RC_INPUT_THR 0
#define RC_INPUT_COUNT 2
volatile uint16_t pulseHighTime[RC_INPUT_COUNT];
volatile uint16_t pulseLowTime[RC_INPUT_COUNT];

// ------------------ENCODER--------------------------
#define PIN_KNOB_A  4       // LSB - digital input for knob clock (must be 2 or 3!))
#define IRQ_KNOB_A  0    //  set IRQ from pin
#define PIN_KNOB_B  10       // MSB - digital input for knob quadrature

enum KNOB_STATES {KNOB_CLICK_0,KNOB_CLICK_1};

volatile char KnobCounter = 0;
volatile char KnobState;

// --------------------------------------------------


//This function pulls the data being populated by the input capture interrupts.
//it corrects for the timer restarting.
inline int pulseRead(int RCindex){return (pulseHighTime[RCindex]>0)?(int)(0.8*pulseHighTime[RCindex]):(int)(0.8*pulseHighTime[RCindex]+0xFFFF);}
//inline int pulseRead(int RCindex){return (int)(0.8*pulseHighTime[RCindex]);}

//interrupt service routine for first input capture module
void __USER_ISR InputCaptureTHR_ISR(void) {
  static uint16_t risingEdgeTime = 0;
  static uint16_t fallingEdgeTime = 0;
  
  clearIntFlag(_INPUT_CAPTURE_1_IRQ);
  if (IC1CONbits.ICBNE == 1)
  {
    if (digitalRead(RC_INPUT_THR) == HIGH)
    {
      risingEdgeTime = IC1BUF;
      pulseLowTime[0] = risingEdgeTime - fallingEdgeTime;
    }
    else
    {
      fallingEdgeTime = IC1BUF;
      pulseHighTime[0] = fallingEdgeTime - risingEdgeTime;
    }
  }
}

//interrupt service routine for second input capture module
void __USER_ISR InputCaptureSTR_ISR(void) {
  static uint16_t risingEdgeTime = 0;
  static uint16_t fallingEdgeTime = 0;
  
  clearIntFlag(_INPUT_CAPTURE_4_IRQ);
  if (IC4CONbits.ICBNE == 1)
  {
    if (digitalRead(RC_INPUT_STR) == HIGH)
    {
      risingEdgeTime = IC4BUF;
      pulseLowTime[1] = risingEdgeTime - fallingEdgeTime;
    }
    else
    {
      fallingEdgeTime = IC4BUF;
      pulseHighTime[1] = fallingEdgeTime - risingEdgeTime;
    }
  }
}

void KnobHandler(void)
{
byte Inputs;
    Inputs = digitalRead(PIN_KNOB_B) << 1 | digitalRead(PIN_KNOB_A);    // align raw inputs
    Inputs ^= 0x02;                             // fix direction

    switch (KnobState << 2 | Inputs)
    {
    case 0x00 : // 0 00 - glitch
        break;
    case 0x01 : // 0 01 - UP to 1
        KnobCounter++;
        KnobState = KNOB_CLICK_1;
        break;
    case 0x03 : // 0 11 - DOWN to 1
        KnobCounter--;
        KnobState = KNOB_CLICK_1;
        break;
    case 0x02 : // 0 10 - glitch
        break;
    case 0x04 : // 1 00 - DOWN to 0
        KnobCounter--;
        KnobState = KNOB_CLICK_0;
        break;
    case 0x05 : // 1 01 - glitch
        break;
    case 0x07 : // 1 11 - glitch
        break;
    case 0x06 : // 1 10 - UP to 0
        KnobCounter++;
        KnobState = KNOB_CLICK_0;
        break;
    default :   // something is broken!
        KnobCounter = 0;
        KnobState = KNOB_CLICK_0;
    }
    
//    displayBinaryOnLEDS( KnobCounter );
    displayBinaryOnLEDS( digitalRead(PIN_KNOB_A) + 2 * digitalRead(PIN_KNOB_B) );

}

// This function read Nbytes bytes from I2C device at address Address. 
// Put read bytes starting at register Register in the Data array. 
void I2Cread(uint8_t Address, uint8_t Register, uint8_t Nbytes, uint8_t* Data)
{
    // Set register address
    Wire.beginTransmission(Address);
    Wire.write(Register);
    Wire.endTransmission();
    
    // Read Nbytes
    Wire.requestFrom(Address, Nbytes); 
    uint8_t index=0;
    while (Wire.available())
        Data[index++]=Wire.read();
}

// Write a byte (Data) in device (Address) at register (Register)
void I2CwriteByte(uint8_t Address, uint8_t Register, uint8_t Data)
{
    // Set register address
    Wire.beginTransmission(Address);
    Wire.write(Register);
    Wire.write(Data);
    Wire.endTransmission();
}

int initIMU() {
     // Set accelerometers low pass filter at 5Hz
    I2CwriteByte(MPU9250_ADDRESS,29,0x06);
    // Set gyroscope low pass filter at 5Hz
    I2CwriteByte(MPU9250_ADDRESS,26,0x06);
    // Configure gyroscope range
    I2CwriteByte(MPU9250_ADDRESS,27,GYRO_FULL_SCALE_1000_DPS);
    // Configure accelerometers range
    I2CwriteByte(MPU9250_ADDRESS,28,ACC_FULL_SCALE_4_G);
    // Set by pass mode for the magnetometers
    I2CwriteByte(MPU9250_ADDRESS,0x37,0x02);
    // Request continuous magnetometer measurements in 16 bits
    I2CwriteByte(MAG_ADDRESS,0x0A,0x16);
}

void displayBinaryOnLEDS(byte n)
{
    for (byte i=0; i<8; i++) {
        digitalWrite( LEDdebugPins[i], n & 1);
        n /= 2;
    }
}

void setup() {
    Wire.begin();
    for (int x = 0; x < 8; x++)
        pinMode( LEDdebugPins[x], OUTPUT);

    // razzle dazzle Night Rider display for 5 seconds        
    for( int j=1; j<3; j++){
        for( int i=0; i<8; i++){
            displayBinaryOnLEDS( pow( 2, i ));
            delay( 125 );
        }
    }
    
    Serial.begin(9600);
    displayBinaryOnLEDS( 0 );
    
    Serial.println( "Starting up..." );
    

        //setup input capture modules one and two
    IC1CON = 0;
    IC1CONbits.ICM = 1;   // Capture an interrupt on every rising and falling edge
    IC1CONbits.ICTMR = 1; // Set to user Timer2
    IC1CONbits.ON = 1;    // Turn IC1 on

    IC4CON = 0;
    IC4CONbits.ICM = 1;   // Capture an interrupt on every rising and falling edge
    IC4CONbits.ICTMR = 1; // Set to user Timer2
    IC4CONbits.ON = 1;    // Turn IC2 on

        /*We're using timer2 for the input capture. This shouldn't interfere with pwm
          output, which uses timers 3-5.
        */
    PR2 = 0xFFFF;         // This tells timer 2 to count up to 0xFFFF, after which it will restart at 0
    T2CONbits.TCKPS = 6;  // 1:64 prescale, which means 80MHz/64 or 1.25MHz clock rate
    T2CONbits.TON = 1;    // Turn on Timer2

    pinMode(RC_INPUT_STR, INPUT);
    pinMode(RC_INPUT_THR, INPUT);

        //these lines set up the interrupt functions to trigger 
    setIntVector(_INPUT_CAPTURE_1_VECTOR, InputCaptureTHR_ISR);
    setIntPriority(_INPUT_CAPTURE_1_VECTOR, 4, 0);
    clearIntFlag(_INPUT_CAPTURE_1_IRQ);
    setIntEnable(_INPUT_CAPTURE_1_IRQ);

    setIntVector(_INPUT_CAPTURE_4_VECTOR, InputCaptureSTR_ISR);
    setIntPriority(_INPUT_CAPTURE_4_VECTOR, 4, 0);
    clearIntFlag(_INPUT_CAPTURE_4_IRQ);
    setIntEnable(_INPUT_CAPTURE_4_IRQ);

// ------------------ENCODER--------------------------
    pinMode(PIN_KNOB_B,INPUT);
    digitalWrite(PIN_KNOB_B,HIGH);  // turn on pullup resistor
    pinMode(PIN_KNOB_A,INPUT);
    digitalWrite(PIN_KNOB_A,HIGH);
    KnobState = digitalRead(PIN_KNOB_A);
    attachInterrupt(0 ,KnobHandler,CHANGE);
    interrupts();
// ---------------------------------------------------

    
    ServoSTR.attach(PIN_STR);
    ServoTHR.attach(PIN_THR);
        
    initIMU();
    gTheOldRCcommand = NOT_ACTUAL_COMMAND;
    gIsInAutonomousMode = false;     
}

void sendSerialCommand( commandDataStruct *theDataPtr ){
    Serial.print(theDataPtr->command);
    Serial.print(",");
    Serial.print(theDataPtr->ax);
    Serial.print(",");
    Serial.print(theDataPtr->ay);
    Serial.print(",");
    Serial.print(theDataPtr->az);
    Serial.print(",");
    Serial.print(theDataPtr->gx);
    Serial.print(",");
    Serial.print(theDataPtr->gy);
    Serial.print(",");
    Serial.print(theDataPtr->gz);
    Serial.print(",");
    Serial.print(theDataPtr->time);
    Serial.print(",");
    Serial.print(theDataPtr->str);
    Serial.print(",");
    Serial.print(theDataPtr->thr);
    Serial.println();
//    Serial.flush();        // Serial.flush halts program until all characters are sent
}

void getSerialCommandIfAvailable( commandDataStruct *theDataPtr ){
    // http://arduino.stackexchange.com/questions/1013/how-do-i-split-an-incoming-string
    int cmd_cnt = 0;
    char cmdBuf[MAX_CMD_BUF];
                
    if (Serial.available()) {        
        byte size = Serial.readBytes(cmdBuf, MAX_CMD_BUF);
        
        if (DEBUG_SERIAL) {
            Serial.write(cmdBuf, size);    //echo what the Pi sent right back to it
        }
            
        // tack on a null byte to the end of the line
        cmdBuf[size] = 0;
    
        // strtok splits a C string into substrings, based on a separator character
        char *command = strtok(cmdBuf, ",");    //  get the first substring

        // loop through the substrings, exiting when the null byte is reached
        //    at the end of each pass strtok gets the next substring
        
        while (command != 0) {        
            switch (cmd_cnt) {
            case CMD_AUTO:
                theDataPtr->command = atoi(command);
                break;
            case CMD_STR:
                theDataPtr->str = atoi(command);    
                if( theDataPtr->str > 2000 || theDataPtr->str < 1000 ){
                    theDataPtr->command = STEERING_VALUE_OUT_OF_RANGE;    
                }
                break;
                
            case CMD_THR:
                theDataPtr->thr = atoi(command);    
                if( theDataPtr->thr > 2000 || theDataPtr->thr < 1000 ){
                    theDataPtr->command = THROTTLE_VALUE_OUT_OF_RANGE;    
                }
                break;
                
            case CMD_TIME:
                theDataPtr->time = atoi(command);    
                break;
                
            default:
                if (DEBUG_SERIAL) {
                    Serial.println("Too many values in command");
                }
                theDataPtr->command = TOO_MANY_VALUES_IN_COMMAND;    
            }
            
            // Get the next substring from the input string
            // changing the first argument from cmdBuf to 0 is the strtok method for subsequent calls
            command = strtok(0, ",");
            cmd_cnt++;

//             if (cmd_cnt == 4) {
//                 if (DEBUG_SERIAL) {
//                     Serial.println();
//                     Serial.print(theDataPtr->command);
//                     Serial.print("/ ");
//                     Serial.print(theDataPtr->str);
//                     Serial.print("/ ");
//                     Serial.print(theDataPtr->thr);
//                     Serial.print("/ ");
//                     Serial.print(theDataPtr->time);
//                     Serial.println();
//                 }
//             }
        }
    }
        
    else{
        theDataPtr->command = NO_COMMAND_AVAILABLE;
    }
}

void handleRCSignals( commandDataStruct *theDataPtr ) {
    
    unsigned long STR_VAL = pulseRead(RC_INPUT_STR-2); // Read pulse width of
    unsigned long THR_VAL = pulseRead(RC_INPUT_THR); // each channel
    
    if (STR_VAL == 0) {    // no steering RC signal 
        if( gTheOldRCcommand != RC_SIGNAL_WAS_LOST ){    // only print RC message once
            if (DEBUG_SERIAL) {
                Serial.println("RC out of range or powered off\n");
            }
            
            gTheOldRCcommand = RC_SIGNAL_WAS_LOST;
        }
        
        theDataPtr->command = RC_SIGNAL_WAS_LOST;
        return;
    }

    // check for reverse ESC signal from RC while in autonomous mode (user wants to stop auto)    
    if ( gIsInAutonomousMode ) {    
        if( THR_VAL > THROTTLE_THRESHOLD_TO_SHUTDOWN_AUTO ){     // signals increase with reverse throttle movement
//            if (DEBUG_SERIAL) {
//                Serial.println("User wants to halt autonomous\n");
//            }
            theDataPtr->command = RC_SIGNALED_STOP_AUTONOMOUS;
            return;
        }
    }

//    Serial.print("ch1:");
//    Serial.print(STR_VAL);
//    Serial.print("ch2:");
//    Serial.print(THR_VAL);
//    Serial.print("\n");

    // clip the RC signals to more car appropriate ones
    if( STR_VAL > MAXIMUM_STEERING_VALUE )
        STR_VAL = MAXIMUM_STEERING_VALUE;

    else if( STR_VAL < MINIMUM_STEERING_VALUE )
        STR_VAL = MINIMUM_STEERING_VALUE;

    if( THR_VAL > MAXIMUM_THROTTLE_VALUE )
        THR_VAL = MAXIMUM_THROTTLE_VALUE;

    else if( THR_VAL < MINIMUM_THROTTLE_VALUE )
        THR_VAL = MINIMUM_THROTTLE_VALUE;
             
    uint8_t Buf[14];
    I2Cread(MPU9250_ADDRESS,0x3B,14,Buf);

    // Create 16 bits values from 8 bits data
    // Accelerometer
    theDataPtr->ax=-(Buf[0]<<8 | Buf[1]);
    theDataPtr->ay=-(Buf[2]<<8 | Buf[3]);
    theDataPtr->az=Buf[4]<<8 | Buf[5];

    // Gyroscope
    theDataPtr->gx=-(Buf[8]<<8 | Buf[9]);
    theDataPtr->gy=-(Buf[10]<<8 | Buf[11]);
    theDataPtr->gz=Buf[12]<<8 | Buf[13];

    // _____________________
    // :::    Magnetometer ::: 
    // Read register Status 1 and wait for the DRDY: Data Ready
    // I2Cread(MAG_ADDRESS,0x02,1,&ST1);
    // Read magnetometer data    
    //uint8_t Mag[7];    
    //I2Cread(MAG_ADDRESS,0x03,7,Mag);        
    // Create 16 bits values from 8 bits data 
    // Magnetometer
    //int16_t mx=-(Mag[3]<<8 | Mag[2]);
    //int16_t my=-(Mag[1]<<8 | Mag[0]);
    //int16_t mz=-(Mag[5]<<8 | Mag[4]);    
    
    theDataPtr->thr = (int) THR_VAL;
    theDataPtr->str = (int) STR_VAL;
    theDataPtr->time = millis();
    theDataPtr->command = GOOD_RC_SIGNALS_RECEIVED;
}

int adjustThrottleForSteering( int steeringValue, int throttleValue ) {
    long adjustedThrottleValue;
    int maxBumpValue = 20;
    
    if( steeringValue > CENTERED_STEERING_VALUE ) 
        adjustedThrottleValue = throttleValue + maxBumpValue * ( steeringValue - CENTERED_STEERING_VALUE ) / ( MAXIMUM_STEERING_VALUE - CENTERED_STEERING_VALUE ); 
    
    else 
        adjustedThrottleValue = throttleValue + maxBumpValue * ( CENTERED_STEERING_VALUE - steeringValue ) / ( CENTERED_STEERING_VALUE - MINIMUM_STEERING_VALUE ); 

    // Serial.print("steeringValue:");
    // Serial.print(steeringValue);
    // Serial.print("  throttleValue:");
    // Serial.print(throttleValue);
    // Serial.print("  adjustedThrottleValue:");
    // Serial.print(adjustedThrottleValue);
    // Serial.print("\n");

    return( (int) adjustedThrottleValue );
}

void loop() {
    
// ------------------------- Handle RC Commands -------------------------------
    commandDataStruct theCommandData;
        
    handleRCSignals( &theCommandData );
    
    //----------LED debugging code-------------
    if( gCountOfPassesForCommandDisplay >= gTotalNumberOfPassesForCommandDisplay / 2 )        // display the command from the RC on the LEDs
//        displayBinaryOnLEDS( theCommandData.command + gIsInAutonomousMode * 128  );
//        displayBinaryOnLEDS( KnobCounter );
//        displayBinaryOnLEDS( digitalRead(PIN_KNOB_A) );
        displayBinaryOnLEDS( digitalRead(PIN_KNOB_A) + 2 * digitalRead(PIN_KNOB_B) );
        
    //----------LED debugging code-------------
        
    //    The signal for stopping autonomous driving is user putting car in reverse
    //       this can be a normal operation in manual driving, so a test for auto mode is made

    if(( theCommandData.command == RC_SIGNALED_STOP_AUTONOMOUS ) || ( theCommandData.command == RC_SIGNAL_WAS_LOST )){
        theCommandData.str = CENTERED_STEERING_VALUE;    //  center the steering
        theCommandData.thr = CENTERED_THROTTLE_VALUE;    //  turn off the motor
        ServoSTR.writeMicroseconds( theCommandData.str );
        ServoTHR.writeMicroseconds( theCommandData.thr );

        if( gIsInAutonomousMode ){    // send the command to pi to stop autonomous
//            if (DEBUG_SERIAL) {
//                Serial.println("Received RC stop while Autonomous mode is on ");
//            }        
                
            theCommandData.command = STOP_AUTONOMOUS;
            delay( 1000 );
            for( int i = 0; i < 5; i++ )	// fire off 5 stop auto commands
                sendSerialCommand( &theCommandData );
            gIsInAutonomousMode = false;
        }
    }
    
    else if( theCommandData.command == GOOD_RC_SIGNALS_RECEIVED ){
       sendSerialCommand( &theCommandData );
       if( gIsInAutonomousMode == false ){	
            //    If not in auto mode, send RC values to servo and ESC
            int adjustedThrottleValue = adjustThrottleForSteering( theCommandData.str, theCommandData.thr );
            ServoSTR.writeMicroseconds( theCommandData.str );
            ServoTHR.writeMicroseconds( adjustedThrottleValue );
        }
    }
    
    else{    // either no command or a bad command was received
    }
    
    delay( 100 );

// ------------------------- Handle Pi Commands -------------------------------
    getSerialCommandIfAvailable( &theCommandData );
    int adjustedThrottleValue = adjustThrottleForSteering( theCommandData.str, theCommandData.thr );
           
    if( theCommandData.command == RUN_AUTONOMOUSLY ){
        ServoSTR.writeMicroseconds( theCommandData.str );
        ServoTHR.writeMicroseconds( adjustedThrottleValue );
        gIsInAutonomousMode = true;
    }
    
    else if( theCommandData.command == STOP_AUTONOMOUS ){
        theCommandData.str = CENTERED_STEERING_VALUE;    //  center the steering
        theCommandData.thr = CENTERED_THROTTLE_VALUE;    //  turn off the motor
        ServoSTR.writeMicroseconds( theCommandData.str );
        ServoTHR.writeMicroseconds( theCommandData.thr );
        gIsInAutonomousMode = false;
    }
    
    else{    // either no command or a bad command was received
    }
        
    //----------LED debugging code-------------
    if( gCountOfPassesForCommandDisplay < gTotalNumberOfPassesForCommandDisplay / 2 )    // display the command from the Pi on the LEDs
//        displayBinaryOnLEDS( theCommandData.command + gIsInAutonomousMode * 128  );
//        displayBinaryOnLEDS( KnobCounter );
        displayBinaryOnLEDS( digitalRead(PIN_KNOB_A) );
//         displayBinaryOnLEDS( 2 * digitalRead(PIN_KNOB_B) );
        
    gCountOfPassesForCommandDisplay = gCountOfPassesForCommandDisplay - 1;
    
    if( gCountOfPassesForCommandDisplay < 0 )    
        gCountOfPassesForCommandDisplay = gTotalNumberOfPassesForCommandDisplay;
    //----------LED debugging code-------------
        
}
