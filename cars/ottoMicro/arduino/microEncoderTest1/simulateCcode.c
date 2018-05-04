#include <stdio.h>

// save code as:    /Users/jim/Dropbox/foocars/cars/ottoMicro/arduino/microEncoderTest1/simulateCcode.c
// go to directory:  cd /Users/jim/Dropbox/foocars/cars/ottoMicro/arduino/microEncoderTest1
// to compile:   gcc -Wall -o simulateCcode simulateCcode.c
// to run:       ./simulateCcode

typedef unsigned char byte;


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


int main(void)    
{
    unsigned char decDigits[6];
    
    unsigned int value = 0x1234;
    printf("%4hx = ", value );
    uBin16_Dec( value, &decDigits[0] );

    for (int i =0; i<5; i++ ){
        printf("%hhx", decDigits[i]);
    }
    printf("\n");
    
    value = 0x7fff;
    printf("%4hx = ", value );
    uBin16_Dec( value, &decDigits[0] );

    for (int i =0; i<5; i++ ){
        printf("%hhx", decDigits[i]);
    }
    printf("\n");
    
    printf("\nHello, world!\n");
    return 0;
}