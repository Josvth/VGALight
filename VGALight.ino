
#include <SPI.h>
#include <Arduino.h>
#include <FastLED.h>
#include <stdint.h>

// All timing values are defined as:
// [duration in us] / ([timer scaler] / 16MHz)

// Timings	
#define COLOUR_FRONT_PORCH		67		// = 4.2us / (1/16MHz)
#define COLOUR_VISIBLE_AREA		256		//256		// = 16us  / (1/16MHz)
#define COLOUR_BACK_PORCH		0

// Delays
//#define HSYNC_PULSE_WIDTH		28		// = 1.7us / (1/16MHz)
#define TIMER2_INITIAL_VALUE	46		// = 2.860 / (1/16MHz)

#define COMPARE_INTERRUPT_DELAY 2		// = 960ns / (1/16MHz)

// We only use Port D for external triggers and pin changes
#define VSYNC PORTD2
#define HSYNC PORTD3

#define LED_DATA_PIN 8

#define RED_CS PORTD4
#define GREEN_CS PORTD5
#define BLUE_CS PORTD6
#define MEASURE_S PORTD7

// State encoding
typedef enum { IDLE, MEASURE, REFRESH } device_state_t;
device_state_t device_state = IDLE;

typedef enum measure_state_t {
	RED_MEASURE		= 1 << RED_CS, 
	GREEN_MEASURE	= 1 << GREEN_CS,
	BLUE_MEASURE	= 1 << BLUE_CS
};

//inline measure_state_t operator|(measure_state_t a, measure_state_t b) {
//	return static_cast<measure_state_t>(static_cast<int>(a) | static_cast<int>(b));
//};
 
measure_state_t measure_state = RED_MEASURE;

// Led strip
#define LEDS_PER_SIDE 14

#define NUM_LEDS 120
#define NUM_LINES_PER_LED 20 

#define TOTAL_HSYNCS  798

#define B_BLANK_HSYNCS		28
#define F_BLANK_HSYNCS		3
#define VISIBLE_HSYNCS		768

CRGB leds[NUM_LEDS];

uint16_t hsync_count = 0;	// Amount of H

volatile uint8_t adc_data = 0;
volatile boolean adc_data_converted = true;

boolean leds_refreshed = false;

void setup(void)
{
	

	// Setup SPI
	SPI.begin();
	SPI.setBitOrder(MSBFIRST);
	SPI.setClockDivider(SPI_CLOCK_DIV2);
	SPI.setDataMode(SPI_MODE1);

	// Setup ports
	DDRD &= ~((1 << VSYNC) | (1 << HSYNC));							// Set trigger ports to inputs
	
	DDRD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS) | (1 << MEASURE_S);	// Set select ports to outputs

	// Set port initials
	PORTD |= (1 << RED_CS) | (1 << GREEN_CS) | (1 << BLUE_CS) | (1 << MEASURE_S);	// Set all adc's into sample and hold mode

	// Setup sample timer1
	TCCR1A	= 0x00;
	TCCR1B	= 0x00;
	TCCR1C	= 0x00;
	TCNT1	= 0x00;
	OCR1A	= 0x00;
	OCR1B	= 0x00;

	TIMSK1 |= (1 << OCIE1B) | (1 << OCIE1A);						// Enable interrupt on compare A and B of timer 1

	OCR1A = COLOUR_FRONT_PORCH - COMPARE_INTERRUPT_DELAY;
	OCR1B = COLOUR_FRONT_PORCH + COLOUR_VISIBLE_AREA - COMPARE_INTERRUPT_DELAY;

	// Setup external interrupts
	EICRA |= (1 << ISC11) | (1 << ISC10) | (1 << ISC01);		// External interrupt on falling edge of VSYNC and HSYNC

	EIMSK |= (1 << INT0);						// Enable VSYNC external interrupt

	FastLED.addLeds<NEOPIXEL, LED_DATA_PIN>(leds, NUM_LEDS);

	for (int i = 0; i < 60; i++) {
		leds[i] = CRGB::Red;
	}

	sei();										// Turn on interrupts

}

boolean in_visible_area = false;

void loop(void)
{
	
	if (device_state == MEASURE) {

		if (!adc_data_converted) { // If we didn't convert the current data we convert it

			//int16_t horizontal_line = hsync_count - B_BLANK_HSYNCS - 1;		// We calculate which line this data belongs to

			//if (hsync_count - B_BLANK_HSYNCS - 1 >= 0 && horizontal_line < VISIBLE_HSYNCS) {	// If this data is from a non visible line we discard it

			//	//int led_index = horizontal_line / (VISIBLE_HSYNCS / LEDS_PER_SIDE);

			//	if (measure_state == RED_MEASURE) {
			//		leds[0].r = adc_data;
			//		//leds[led_index + 60].r += normalized_data;
			//	}
			//	else if (measure_state == GREEN_MEASURE) {
			//		leds[0].g = adc_data;
			//		//leds[led_index + 60].g += normalized_data;
			//	}
			//	else if (measure_state == BLUE_MEASURE) {
			//		leds[0].b = adc_data;
			//		//leds[current_led + 60].b += normalized_data;
			//	}
			//	
			//}

			if (measure_state == RED_MEASURE) {
				leds[10].r = adc_data;
				//leds[led_index + 60].r += normalized_data;
			}
			else if (measure_state == GREEN_MEASURE) {
				leds[10].g = adc_data;
				//leds[led_index + 60].g += normalized_data;
			}
			else if (measure_state == BLUE_MEASURE) {
				leds[10].b = adc_data;
				//leds[current_led + 60].b += normalized_data;
			}

			adc_data_converted = true;

		}
		
		return;
	}

	if (device_state == REFRESH) {

		EIMSK &= ~(1 << INT0);						// Enable VSYNC external interrupt

		if (!leds_refreshed) {

			FastLED.show();

			leds_refreshed = true;
		}
		
		EIMSK |= (1 << INT0);						// Enable VSYNC external interrupt

		return;
	}

}

// Falling edge VSYNC
ISR(INT0_vect) 
{	

	EIMSK &= ~(1 << INT1);						// Disable HSYNC external interrupt	
	TCCR1B = 0x00;								// Disable timer 1

	hsync_count = 3;							// One VSYNC pulse contains exactly 4 HSYNCS
	
	if (device_state == IDLE || device_state == REFRESH) {

		device_state = MEASURE;
		
		//PORTD |= (1 << GREEN_CS);		// Put green the adc in hold mode
		
		EIMSK |= (1 << INT1);						// Enable HSYNC external interrupt	
		
	} else if (device_state == MEASURE) {
		
		if (measure_state == RED_MEASURE) {
			measure_state = GREEN_MEASURE;
			EIMSK |= (1 << INT1);						// Enable HSYNC external interrupt	
		}
		else if (measure_state == GREEN_MEASURE) {
			measure_state = BLUE_MEASURE;
			EIMSK |= (1 << INT1);						// Enable HSYNC external interrupt	
		}
		else if (measure_state == BLUE_MEASURE) {
			
			measure_state = RED_MEASURE;

			leds_refreshed = false;

			device_state = REFRESH;

			//PORTD &= ~(1 << GREEN_CS);		// Put the green adc in sample mode
		}

	}

}

// Falling edge HSYNC
ISR(INT1_vect, ISR_NAKED) 
{
	char cSREG = SREG;	// We templorary store our status register
	
	PORTD |= (1 << BLUE_CS);			// Put red the adc in hold mode

	TCNT1 = TIMER2_INITIAL_VALUE;			// Set timer 1 to zero plus our offset due to falling edge triggering and trigger delay
	TCCR1B |= (1 << CS10);					// Enable timer 1 with a clock of 16MHz
	
	hsync_count++;

	PORTD &= ~BLUE_MEASURE;		// Put the green adc in sample mode
	
	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// Begin of averaging
ISR(TIMER1_COMPA_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	PORTD |= (1 << BLUE_CS);			// Put red the adc in hold mode

	PORTD &= ~(1 << MEASURE_S);	// We start averaging
	
	adc_data |= SPI.transfer(0xFF) << 2;
	adc_data |= SPI.transfer(0xFF) >> 6;

	adc_data_converted = false;
	
	PORTD &= ~(1 << BLUE_CS);		// Put the green adc in sample mode
	
	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

// End of averaging start of sample
ISR(TIMER1_COMPB_vect, ISR_NAKED)
{
	char cSREG = SREG;	// We templorary store our status register

	PORTD |= (1 << BLUE_CS);			// Put red the adc in hold mode

	TCCR1B = 0x00;						// Disable timer 1

	PORTD |= RED_MEASURE;				// Put the current adc in sample and hold mode
	PORTD |= RED_MEASURE;				// Put the current adc in sample and hold mode
	PORTD &= ~RED_MEASURE;			// Put the current adc in convert mode

	PORTD |= (1 << MEASURE_S);			// We end averaging

	PORTD &= ~(1 << BLUE_CS);		// Put the green adc in sample mode

	SREG = cSREG;		// We restore our status register
	reti();				// We jump back
}

