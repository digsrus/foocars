#include <Arduino.h>
//#include <SoftPWMServo.h>
#include <Wire.h>
#include <Servo.h>



const int PIN_STR = 9;
const int PIN_THR = 7;

// oddball pin arrangement is to have each byte have contiguous pins:
byte LEDdebugPins[] = { 24,  25,  26,  27, A14, A13, A12, A11,  A7,  A6,  A5,  A4,  A3,  A2,  A1,  A0 };    // lsb is pin 24

Servo ServoSTR;
Servo ServoTHR;

// ------------------ENCODER--------------------------
#define PIN_Encoder_A  1       // pin 1 -> interrupt INT2
#define PIN_Encoder_C  2       // pin 2 -> interrupt INT3
#define IRQ_Encoder_A  2       //  INT2
#define IRQ_Encoder_C  3       //  INT3
#define PIN_Encoder_B  5       // MSB - digital input for knob quadrature

enum Encoder_STATES {Encoder_CLICK_0,Encoder_CLICK_1};

volatile int gEncoderCounter = 0;
volatile char gEncoderState;

// --------------------------------------------------



void EncoderInterruptHandler(void)
{
    byte Inputs;
    Inputs = digitalRead(PIN_Encoder_B) << 1 | digitalRead(PIN_Encoder_A);    // align raw inputs
//    Inputs ^= 0x02;                             // fix direction

    switch (gEncoderState << 2 | Inputs)
    {
    case 0x00 : // 0 00 - glitch
        break;
    case 0x01 : // 0 01 - UP to 1
        gEncoderCounter++;
        gEncoderState = Encoder_CLICK_1;
        break;
    case 0x03 : // 0 11 - DOWN to 1
        gEncoderCounter--;
        gEncoderState = Encoder_CLICK_1;
        break;
    case 0x02 : // 0 10 - glitch
        break;
    case 0x04 : // 1 00 - DOWN to 0
        gEncoderCounter--;
        gEncoderState = Encoder_CLICK_0;
        break;
    case 0x05 : // 1 01 - glitch
        break;
    case 0x07 : // 1 11 - glitch
        break;
    case 0x06 : // 1 10 - UP to 0
        gEncoderCounter++;
        gEncoderState = Encoder_CLICK_0;
        break;
    default :   // something is broken!
        gEncoderCounter = 0;
        gEncoderState = Encoder_CLICK_0;
    }
}

void uBin16_Dec( unsigned int Arg, unsigned char* sptr)
{
    *sptr = 0;
    while (Arg & 0xC000)                //    while (Arg > 10000)
    {    Arg -= 10000;
        *sptr += 1;
    }
    if (Arg >= 10000)
    {    Arg -= 10000;
        *sptr += 1;
    }

    sptr++;
    *sptr = 0;
    while (Arg & 0x3C00)                // ((int)Arg > 0)
    {    Arg -= 1000;
        *sptr += 1;
    }
    if (Arg >= 1000)
    {    Arg -= 1000;
       *sptr += 1; }

    sptr++;
    *sptr = 0;
    while (Arg & 0x0780)                // ((int)Arg > 0)
    {    Arg -= 100;
        *sptr += 1;
    }
    if (Arg >= 100)
    {    Arg -= 100;
       *sptr += 1; }

    sptr++;
    *sptr = 0;
    while (Arg & 0x70)                    // (Arg > 0)
    {    Arg -= 10;
        *sptr += 1;
    }
    if (Arg >= 10)
    {    Arg -= 10;
       *sptr += 1; }

    sptr++;
    *sptr = (unsigned char)Arg;
}  

void displayBinaryOnLEDS( unsigned int value ) {
    unsigned char lowByte, hiByte;
    lowByte = value & 0x00ff;
    hiByte = ( value >> 8 );
    
    for (byte i=0; i<8; i++) {
        digitalWrite( LEDdebugPins[i], lowByte & 1);
        lowByte /= 2;
    }

    for (byte i=8; i<16; i++) {
        digitalWrite( LEDdebugPins[i], hiByte & 1);
        hiByte /= 2;
    }
}    

void displayBinaryOnLEDSasDecimal( unsigned int value ) {
    unsigned char decDigits[5];
    uBin16_Dec( value, &decDigits[0] );
    unsigned char loByte = decDigits[4] + ( decDigits[3] << 4 ); 
    unsigned char hiByte = decDigits[2] + ( decDigits[1] << 4 );
    displayBinaryOnLEDS(( hiByte << 8 ) + loByte );
}

void setup() {
    for (int x = 0; x < 16; x++)
        pinMode( LEDdebugPins[x], OUTPUT);


    Serial.begin(9600);
//    displayBinaryOnLEDS( 0 );
    Serial.println( "Starting up..." );



// ------------------ENCODER--------------------------
    pinMode( PIN_Encoder_B,INPUT );
    digitalWrite( PIN_Encoder_B,HIGH );  // turn on pullup resistor
    pinMode( PIN_Encoder_A,INPUT );
    digitalWrite( PIN_Encoder_A,HIGH );
    pinMode( PIN_Encoder_C,INPUT );
    digitalWrite( PIN_Encoder_C,HIGH );
    gEncoderState = digitalRead( PIN_Encoder_A );
    attachInterrupt( IRQ_Encoder_A ,EncoderInterruptHandler,RISING );
    attachInterrupt( IRQ_Encoder_C ,EncoderInterruptHandler,FALLING );
// ---------------------------------------------------

    ServoSTR.attach(PIN_STR);
    ServoTHR.attach(PIN_THR);
}


void loop() {
    
    int signalValue[] = { 1600, 1603, 1606, 1609, 1612, 1615 };   
    unsigned int tEncoderCounter;
        
    for (int i = 0; i < 6; i++){
        ServoSTR.writeMicroseconds( 1300 );
        delay (1500);
        
        ServoSTR.writeMicroseconds( 1700 );
        delay (1500);
        
        ServoSTR.writeMicroseconds( 1500 );
        
        ServoTHR.writeMicroseconds( signalValue[i] );   // do it twice just in case
        ServoTHR.writeMicroseconds( signalValue[i] );
        
        delay( 500 );  // delay 1/2 second to get up to speed
        
        noInterrupts();
        gEncoderCounter = 0;
        long startingMillis = millis();
        interrupts();
        
        //  measure time it takes to get 10000 counts ( about 6' I think)
        boolean notDone = true;
        while( notDone ){
           noInterrupts();
           if( gEncoderCounter > 9999 )
                notDone = false;
                
            else
                tEncoderCounter = (unsigned int) gEncoderCounter;
           interrupts();
    
            // adjust speed every .1 second    
            if((( millis() - startingMillis ) % 100 ) == 0 ){
                displayBinaryOnLEDSasDecimal( tEncoderCounter );     // time in seconds
  
            }        
        }
        
        long timedMillis = millis() - startingMillis;
        ServoTHR.writeMicroseconds( 1500 );     // shut her down
        
        for (int j = 0; j < 6; j++){        // show the interim results 5 times - watch out next test starts automatically
            displayBinaryOnLEDSasDecimal( signalValue[i] );
            delay(500);
            
            displayBinaryOnLEDSasDecimal( timedMillis / 10 );     // time in seconds
            delay(1000);
        }
    }
    
    while( true )           // loop forever until reset
        ;
}
