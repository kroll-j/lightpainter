#include <math.h>
#include <stdlib.h>
#include "main.h"

// rgb led resistor values....
// G: 2.2 + 1.0 parallel
// B: 4.7 + 4.7 + 5.6 parallel
// R: 8.2 + 10 parallel

#define ADB_PIN     PB2     // pin number
#define ADB_PINREG  PINB    // pin register
#define ADB_PORT    PORTB   // port register
#define ADB_PDIR    DDRB    // data direction register
#define ADB_TCNT    TCNT0   // timer counter register
#define TIMER_DIV   64      // timer clock divisor
#include "tm1001a.h"

#define CLAMP(V, min, max) do { if(V<min) V= min; if(V>max) V= max; } while(0);

#define TOUCHPAD_XMIN   250
#define TOUCHPAD_XMAX   6500
#define TOUCHPAD_YMIN   500
#define TOUCHPAD_YMAX   4500

#define RGB_BITS        14
#define RGB_MAX         ((1<<RGB_BITS)-1)
#define HSV_BITS        14
#define HSV_MAX         ((1<<HSV_BITS)-1)

#define RESET_PINREG    PINF
#define RESET_DDR       DDRF
#define RESET_PORT      PORTF
#define RESET_PIN       4

//~ #define printf(x...)
//~ #define puts(x...)

struct buttondesc
{
    volatile uint8_t *ddrReg;
    volatile uint8_t *portReg;
    volatile uint8_t *pinReg;
    uint8_t pin;
};

static const struct buttondesc buttons[]=
{
    { &DDRB, &PORTB, &PINB, 4 },
    { &DDRE, &PORTE, &PINE, 6 },
    { &DDRD, &PORTD, &PIND, 4 },
    { &DDRD, &PORTD, &PIND, 0 },
};

void buttonSetup(void)
{
    for(int i= 0; i<sizeof(buttons)/sizeof(buttons[0]); ++i)
    {
        *buttons[i].ddrReg &= ~(1<<buttons[i].pin);
        *buttons[i].portReg |= 1<<buttons[i].pin;
    }
}

uint8_t buttonRead(void)
{
    uint8_t ret= 0;
    for(int i= 0; i<sizeof(buttons)/sizeof(buttons[0]); ++i)
    {
        uint8_t val= (*buttons[i].pinReg & (1<<buttons[i].pin)) >> buttons[i].pin;
        ret|= (val^1) << i;
    }
    return ret;
}

void touchpadTimerSetup(void)
{
    TCCR0A= 0;
    TCCR0B= (1<<CS01) | (1<<CS00);  // set timer0 clock divisor to 64
}

void touchpadInitADB(void)
{
	uint8_t adbData[8];
    
    // Auf Absolutmodus umschalten
    adbData[0] = 0b01100011;    // enabled, device addr 3
    adbData[1] = 4; //CDM Modus
    adbExecuteCommand(COM_LISTEN3, adbData, 2);

    adbExecuteCommand(COM_TALK1, adbData, 0); // Werte holen

    adbData[6] = 0x00; //Absolutmodus
    adbExecuteCommand(COM_LISTEN1, adbData, 7);
}

// timer1: fast pwm mode
//  OC1A: PB5=D9=RED
//  OC1B: PB6=D10=GREEN
// timer3: fast pwm mode
//  OC3A: PC6=D5=BLUE
void setupPWM(void)
{
    TCCR1A= (1<<COM1A1) | (1<<COM1B1) |     // Clear OC1A/OC1B on compare match, set OC1A/OC1B at TOP
            (1<<WGM11);                     // Fast PWM
    TCCR1B= (1<<WGM13) | (1<<WGM12) |       // Fast PWM, TOP=ICR1
            (1<<CS10);                      // No Prescaling
    ICR1= RGB_MAX;                          // Timer TOP value, f=~976Hz
    DDRB|= (1<<5) | (1<<6);                 // Enable RED and GREEN outputs
    
    TCCR3A= (1<<COM3A1) |                   // Clear OC3A on compare match, set OC3B at TOP
            (1<<WGM31);                     // Fast PWM
    TCCR3B= (1<<WGM33) | (1<<WGM32) |       // Fast PWM, TOP=ICR3
            (1<<CS30);                      // No Prescaling
    ICR3= RGB_MAX;                          // Timer TOP value, f=~976Hz
    DDRC|= (1<<6);                          // Enable BLUE output
    
    OCR1A= 2048;    // initial RED
    OCR1B= 6144;    // initial GREEN
    OCR3A= 0;       // initial BLUE
}

void setLEDs(int16_t r, int16_t g, int16_t b)
{
    OCR1A= r;
    OCR1B= g;
    OCR3A= b;
}

void hsv2rgb(int h, int s, int v, uint16_t *dest)
{
    struct rgb
    {
        int16_t r, g, b;
    };

    static struct rgb interp[7]=  { { HSV_MAX, 0, 0 },           // red
                                  { HSV_MAX, 0, HSV_MAX },       // red/blue
                                  { 0, 0, HSV_MAX },             // blue
                                  { 0, HSV_MAX, HSV_MAX },       // blue/green
                                  { 0, HSV_MAX, 0},              // green
                                  { HSV_MAX, HSV_MAX, 0},        // green/red
                                  { HSV_MAX, 0, 0 } };           // red
    int index;    // table index
    int offset;   // between entries
    uint32_t r, g, b;
    
    //~ if(v>HSV_MAX) v= HSV_MAX;      // Helligkeit innerhalb max. Werte halten
    //~ else if(v<0) v= 0;
    CLAMP(v, 0, HSV_MAX);
    h&= HSV_MAX;                   // modulo mit max. Wert
    s= (HSV_MAX+1)-s;              // Saturation invertieren
    index= h / ((HSV_MAX+1)/6);    // index
    offset= h % ((HSV_MAX+1)/6);   // und Abstand berechnen
    
    // hue
    r= interp[index].r + (int32_t)(interp[index+1].r-interp[index].r)*offset/(HSV_MAX/6);
    g= interp[index].g + (int32_t)(interp[index+1].g-interp[index].g)*offset/(HSV_MAX/6);
    b= interp[index].b + (int32_t)(interp[index+1].b-interp[index].b)*offset/(HSV_MAX/6);

    // value
    r= (int32_t)(r)*v>>HSV_BITS;
    g= (int32_t)(g)*v>>HSV_BITS;
    b= (int32_t)(b)*v>>HSV_BITS;

    // saturation
    r= r + ((int32_t)(v-r)*s>>HSV_BITS);
    g= g + ((int32_t)(v-g)*s>>HSV_BITS);
    b= b + ((int32_t)(v-b)*s>>HSV_BITS);

    dest[0]= r>>(HSV_BITS-RGB_BITS); dest[1]= g>>(HSV_BITS-RGB_BITS); dest[2]= b>>(HSV_BITS-RGB_BITS);
}

void setLEDsHSV(uint16_t h, uint16_t s, uint16_t v)
{
    uint16_t rgb[3];
    hsv2rgb(h, s, v, rgb);
    setLEDs(rgb[0], rgb[1], rgb[2]);
}

uint8_t extractSingleButton(uint8_t mask)
{
    int button= 0;
    while(mask && !(mask&(1<<button)) )
        ++button;
    if(mask & ~(1<<button))
        return 0;
    return button+1;
}

static int32_t h[4], 
               s[4]= { HSV_MAX/2, HSV_MAX/2, HSV_MAX/2, HSV_MAX/2 },
               v[4]= { HSV_MAX, HSV_MAX, HSV_MAX, HSV_MAX };

void dragAction(uint16_t motionBeginX, uint16_t motionBeginY, uint16_t currentX, uint16_t currentY, int16_t relX, int16_t relY, 
                uint16_t pressure, uint8_t buttons, uint8_t isBegin, uint8_t isEnd)
{
    static const uint16_t BOUNDARY1= (TOUCHPAD_XMAX-TOUCHPAD_XMIN)/3+TOUCHPAD_XMIN, 
                          BOUNDARY2= (TOUCHPAD_XMAX-TOUCHPAD_XMIN)*2/3+TOUCHPAD_XMIN;
    
    
    if(isEnd && !buttons)
    {
        setLEDs(0,0,0);
        return; ////////////////////
    }
    
    uint8_t button= extractSingleButton(buttons);
    
    static int32_t lh, ls, lv;
    if(button)
        lh= h[button-1],
        ls= s[button-1],
        lv= v[button-1];

    int16_t arelY= relY; //*2;
    //~ const int16_t scaling[][2]= { { 20, 2 }, { 40, 4 }, { 80, 6 }, { 160, 8 }, { 320, 20 } };
    //~ for(int8_t i= sizeof(scaling)/sizeof(scaling[0])-1; i>=0; --i)
        //~ if(abs(relY)>scaling[i][0]) { arelY*= scaling[i][1]; break; }
    //~ printf("arelY: %d\n", arelY);
    
    if(motionBeginX<BOUNDARY1)
    {
        // top region controls value (brightness)
        lv+= arelY;
        CLAMP(lv, 0, HSV_MAX*2); // allow brightness up to 200%
    }
    else if(motionBeginX<BOUNDARY2)
    {
        // middle region controls hue
        lh+= arelY;
        lh&= HSV_MAX;
    }
    else
    {
        // bottom region controls saturation
        ls+= arelY;
        CLAMP(ls, 0, HSV_MAX);
    }
    if(button)
        h[button-1]= lh,
        s[button-1]= ls,
        v[button-1]= lv;

    setLEDsHSV(lh, ls, lv);

    //~ printf("h: %ld s: %ld v: %ld\n", h[button], s[button], v[button]);
    //~ setLEDsHSV(h[button], s[button], v[button]);
}

void statusLED(bool on)
{
    if(on)
        PORTB&= ~(1<<0);
    else
        PORTB|= 1<<0;
}

void setup(void)
{
    touchpadTimerSetup();
    Delay_MS(100);  // wait a bit -- the touchpad seems to take a while to power up
    touchpadInitADB();
    setupPWM();
    buttonSetup();
    
    RESET_PINREG|= (1<<RESET_PIN);
    RESET_DDR|= (1<<RESET_PIN);
    
    // status LED
    DDRB|= (1<<0);
    statusLED(false);

    dragAction((TOUCHPAD_XMIN-TOUCHPAD_XMIN)/2, (TOUCHPAD_YMIN-TOUCHPAD_YMIN)/2, (TOUCHPAD_XMIN-TOUCHPAD_XMIN)/2, (TOUCHPAD_YMIN-TOUCHPAD_YMIN)/2, 
                0, 0, 100, 
                0/*buttons*/, 1/*isBegin*/, 0/*isEnd*/);
    
    dragAction((TOUCHPAD_XMIN-TOUCHPAD_XMIN)/2, (TOUCHPAD_YMIN-TOUCHPAD_YMIN)/2, (TOUCHPAD_XMIN-TOUCHPAD_XMIN)/2, (TOUCHPAD_YMIN-TOUCHPAD_YMIN)/2, 
                0, 0, 100, 
                0/*buttons*/, 0/*isBegin*/, 1/*isEnd*/);
}

void tick(void)
{
    //~ setLEDs(RGB_MAX,RGB_MAX,RGB_MAX);
    //~ setLEDs(RGB_MAX/2,RGB_MAX/2,RGB_MAX/2);
    //~ setLEDs(0,0,0);
    //~ return; ///////////////////////////////////
    
    static uint8_t wasDown;
    static uint16_t motionBeginX, motionBeginY;
    static uint16_t lastX, lastY;
    static uint8_t lastButtonState;
    
    uint8_t buttons= buttonRead();
    if(buttons!=lastButtonState)
    {
        if(buttons)
        {
            uint8_t button= extractSingleButton(buttons);
            if(button)
            {
                printf("button %d pressed\n", button);
                setLEDsHSV(h[button-1], s[button-1], v[button-1]);
            }
            else
                printf("multiple buttons\n");
        }
        else
        {
            puts("buttons off");
            setLEDs(0, 0, 0);
        }
        lastButtonState= buttons;
    }
        
    
    uint8_t adbData[8];
    struct adbAbsMode absData;
    char res= adbPoll(adbData);
    if(res)
    {
        if(res<0)
            puts("error polling adb");
        else
        {
            adbGetAbsModeData(&absData, adbData);
            if(absData.pressure && absData.xpos && absData.ypos)
            {
                if(!wasDown)
                    motionBeginX= absData.xpos,
                    motionBeginY= absData.ypos;
                dragAction(motionBeginX, motionBeginY, absData.xpos, absData.ypos, 
                            (wasDown? absData.xpos-lastX: 0), (wasDown? absData.ypos-lastY: 0), absData.pressure, 
                            buttons, !wasDown/*isBegin*/, 0/*isEnd*/);
                wasDown= 1;
                lastX= absData.xpos;
                lastY= absData.ypos;
            }
            else
            {
                if(wasDown)
                    dragAction(motionBeginX, motionBeginY, lastX, lastY, 0, 0, 0, 
                                buttons/*buttons*/, 0/*isBegin*/, 1/*isEnd*/);
                wasDown= 0;
            }
        }
    }
}

bool ProcessCDCLine(const char *line)
{
    statusLED(true);
    Delay_MS(100);
    statusLED(false);
    if(!strcmp(line, "R"))
        setLEDs(RGB_MAX, 0, 0);
    else if(!strcmp(line, "G"))
        setLEDs(0, RGB_MAX, 0);
    else if(!strcmp(line, "B"))
        setLEDs(0, 0, RGB_MAX);
    else if(!strcmp(line, "W"))
        setLEDs(RGB_MAX, RGB_MAX, RGB_MAX);
    else if(!strcmp(line, "OFF"))
        setLEDs(0, 0, 0);
    else if(!strcmp(line, "reset") || !strcmp(line, "r"))
        RESET_PORT&= ~(1<<RESET_PIN);
    else
        return false;
    
    return true;
}

void ProcessCDCChar(uint8_t c)
{
    #define LINE_MAX 32
    static char linebuffer[LINE_MAX+1];
    static int offset= 0;
    
    if(c=='\n' || c=='\r')
    {
        linebuffer[offset&(LINE_MAX-1)]= 0;
        if(strlen(linebuffer))
        {
            if(!ProcessCDCLine(linebuffer))
                printf("invalid command '%s'\n", linebuffer);
            else
                printf("'%s' OK\n", linebuffer);
        }
        offset= 0;
    }
    else
        linebuffer[(offset++)&(LINE_MAX-1)]= c;
}

