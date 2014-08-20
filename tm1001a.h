#ifndef TM1001A_H
#define TM1001A_H

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>

#if(!defined(ADB_PIN) || !defined(ADB_PINREG) || !defined(ADB_PORT) || !defined(ADB_PDIR) || !defined(ADB_TCNT) || !defined(TIMER_DIV))
#error "need to #define necessary stuff before including this"
#endif


// compile-time assertion macro
#define CASSERT(x, name) struct cassert_##name { int name: x; };

#define microsToTicks(u)	( (u) / (TIMER_DIV / (F_CPU/1000000) ) )

#define ADB_PULSE_SHORT		microsToTicks(35)
#define ADB_PULSE_LONG		microsToTicks(65)
#define ADB_PULSE_ATT		microsToTicks(570)
#define ADB_PULSE_TIMEOUT	microsToTicks(250)

CASSERT(ADB_PULSE_ATT<256, pulseval);


void adbPinHi(void)
{
	ADB_PDIR&= ~(1<<ADB_PIN);	    // set pin as input
	ADB_PORT|= (1<<ADB_PIN);        // enable internal pullup resistor
}

void adbPinLo(void)
{
	ADB_PDIR|= (1<<ADB_PIN);		// set pin as output
	ADB_PORT&= ~(1<<ADB_PIN);       // set pin low
}

uint8_t adbPin(void)         		// get adb pin state
{
	return (ADB_PINREG & (1<<ADB_PIN));
}


void adbWriteBit(uint8_t b)
{
    uint8_t t= ADB_TCNT + (b? ADB_PULSE_SHORT: ADB_PULSE_LONG);

	adbPinLo();
	while(ADB_TCNT>t) ;
	while(ADB_TCNT<t) ;

	t= ADB_TCNT + (b? ADB_PULSE_LONG: ADB_PULSE_SHORT);
    adbPinHi();
	while(ADB_TCNT>t) ;
	while(ADB_TCNT<t) ;
}


void adbWriteByte(uint8_t b)
{
	// write data bits
	for(int i= 128; i; i>>= 1)
		adbWriteBit( b&i );
}



// ADB Befehle
#define COM_TALK0 0b00111100    // Adresse 3, Talk, Register0
#define COM_TALK1 0b00111101    // Adresse 3, Talk, Register1
#define COM_LISTEN1 0b00111001  // Adresse 3, Listen, Register1
#define COM_TALK2 0b00111110    // Adresse 3, Talk, Register2
#define COM_TALK3 0b00111111    // Adresse 3, Talk, Register3
#define COM_LISTEN3 0b00111011  // Adresse 3, Listen, Register3

#define ADBCMD_TALK		0b1100
#define ADBCMD_LISTEN	0b1000
#define ADB_cmdByte(cmd, address, register) ( (cmd) | ((address)<<4) | (register) )

#define adbOnPinTimeout(cond, timeout, action...)   \
{                                                   \
        t= ADB_TCNT + timeout;                      \
        while((ADB_TCNT>t) && cond) ;               \
        if(cond) while((ADB_TCNT<t) && cond) ;      \
        if(cond) action;                            \
}

// sends a command byte + nBytesOut of data
// puts received data in 'data'
// returns number of received bytes or -1 on error
int8_t adbExecuteCommand(uint8_t cmdByte, uint8_t *data, uint8_t nBytesOut)
{
    uint8_t t;
    int8_t nBytesRead= 0;
    
	cli();

    // send attention signal
    t= ADB_TCNT + ADB_PULSE_ATT;

	adbPinLo();
	while(ADB_TCNT>t) ;
	while(ADB_TCNT<t) ;

	// write start bit
	adbWriteBit(1);

	// write command byte
	adbWriteByte(cmdByte);

    // write stop bit
    adbWriteBit(0);

	if(nBytesOut)
	{
        // write start bit
        adbWriteBit(1);

        // write data bytes
		for(t= 0; t<nBytesOut; t++)
			adbWriteByte(data[t]);

		// write stop bit
		adbWriteBit(0);
	}
	else
	{
		// wait for data
        adbOnPinTimeout(adbPin(), ADB_PULSE_TIMEOUT, 
            { nBytesRead= 0; goto ret; }
        );
        
		// read start bit
        adbOnPinTimeout(!adbPin(), ADB_PULSE_TIMEOUT, 
            { nBytesRead= -1; goto ret; }
        );
        
		for(nBytesRead= 0; nBytesRead<8; nBytesRead++)
		{
            data[nBytesRead]= 0;
			for(uint8_t i= 128; i; i>>= 1)
			{
                adbOnPinTimeout(adbPin(), ADB_PULSE_SHORT+ADB_PULSE_LONG, 
                    goto ret;
                );

                adbOnPinTimeout(!adbPin(), ADB_PULSE_SHORT+ADB_PULSE_LONG,
                    goto ret;
                );

                t-= ADB_PULSE_SHORT+ADB_PULSE_LONG;
                if( (uint8_t)(ADB_TCNT-t) < (ADB_PULSE_SHORT+ADB_PULSE_LONG)/2 )
                    data[nBytesRead]|= i;
			}
		}
	}

    ret:
        sei();
        return nBytesRead;
}


uint8_t adbPoll(uint8_t *output)
{
	// one poll should block for about 
	// ADB_PULSE_ATT + (ADB_PULSE_SHORT+ADB_PULSE_LONG)*10 + ADB_PULSE_TIMEOUT = 
	// 570 + (35+65)*10 + 250 =
	// 1820 microseconds,
	// plus the time of receiving data when a finger is on the pad
	return adbExecuteCommand( ADB_cmdByte(ADBCMD_TALK, 3, 0),
							  output, 0 );
}


struct adbAbsMode
{
    int xpos, ypos, pressure;
    unsigned int button: 1, gesture: 1;
};

void adbGetAbsModeData(struct adbAbsMode *ret, uint8_t *adbData)
{
    ret->xpos= ((adbData[1] & 0x7F) << 2) |
               ((adbData[2] & 7) << 9) |
               ((adbData[3] & 7) << 12);

    ret->ypos= ((adbData[0] & 0x7F) << 2) |
               ((adbData[2] & 0b1110000) << (9-4)) |
               ((adbData[3] & 0b1110000) << (12-4));

    ret->pressure= ((adbData[4] & 7) << 2) |
                   ((adbData[4] & 0b1110000) << (5-4));

    ret->button= adbData[0]>>7;
    ret->gesture= adbData[1]>>7;
}


#endif //TM1001A_H
