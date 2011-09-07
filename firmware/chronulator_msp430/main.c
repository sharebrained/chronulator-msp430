/*
 * Copyright (C) 2007 ShareBrained Technology, Inc.
 * 
 * This file is part of the Chronulator PM2V software package.
 * 
 * The Chronulator PM2V software package is free software; you
 * can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * The Chronulator PM2V software package is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
 
#include "msp430x20x2.h"

__interrupt void Watchdog_ISR(void);

typedef enum meter_mode
{
	METER_MODE_SHOW_TIME = 0,
	METER_MODE_CALIBRATE_ZERO_SCALE = 1,
	METER_MODE_CALIBRATE_FULL_SCALE = 2
} meter_mode_t;

static meter_mode_t meter_mode = METER_MODE_SHOW_TIME;

static const unsigned char debounce_wait = 16;

static unsigned char debounce_counter_s1 = 0;
static unsigned char debounce_counter_s2 = 0;

static unsigned char s1_active = 0;
static unsigned char s2_active = 0;

unsigned char timer_count_h = 0;
unsigned char timer_count_m = 0;

// Variables to store the current time, along with defaults
// for when the device is first powered up.
static unsigned char hour = 6;
static unsigned char minute = 30;
static unsigned char second = 0;

inline void tick_hour(void)
{
	if( hour < 11 )
	{
		hour++;
	}
	else
	{
		hour = 0;
	}
}

inline void tick_minute(void)
{
	if( minute < 59 )
	{
		minute++;
	}
	else
	{
		minute = 0;
		tick_hour();
	}
}

inline void tick_second(void)
{
	if( second < 59 )
	{
		second++;
	}
	else
	{
		second = 0;
		tick_minute();
	}
}

inline void add_hour(void)
{
	if( hour < 11 )
	{
		hour++;
	}
	else
	{
		hour = 0;
	}
}

inline void subtract_hour(void)
{
	if( hour > 0 )
	{
		hour--;
	}
	else
	{
		hour = 11;
	}
}

inline void add_minute(void)
{
	// Increment the minute. If we're already at or above
	// 59, set it to zero instead.
	if( minute < 59 )
	{
		minute++;
	}
	else
	{
		minute = 0;
	}
}

inline void subtract_minute(void)
{
	if( minute > 0 )
	{
		minute--;
	}
	else
	{
		minute = 59;
	}
}

void show_time()
{
	// We're in time display mode, so compute the timer values.
	// Because of the trickery described above, we're computing
	// the *off* time here, not the on-time. That's why we're
	// subtracting from 64 -- there's 64 timer counts (32.768KHz)
	// for each interrupt.
	timer_count_m = 64 - minute;
	timer_count_h = 64 - (hour * 5);
}

void set_mode_show_time()
{
	meter_mode = METER_MODE_SHOW_TIME;
	show_time();
}

void set_mode_calibrate_zero_scale()
{
	meter_mode = METER_MODE_CALIBRATE_ZERO_SCALE;
	timer_count_m = 64 - 0;
	timer_count_h = 64 - 0;
}

void set_mode_calibrate_full_scale()
{
	meter_mode = METER_MODE_CALIBRATE_FULL_SCALE;
	timer_count_m = 64 - 60;
	timer_count_h = 64 - 60;
}

inline void s1_pressed(void)
{
	switch( meter_mode )
	{
	case METER_MODE_SHOW_TIME:
		if( s2_active )
		{
			// Undo minute increment. We wound up incrementing
			// it on the way in to calibration mode, before the
			// second button was held down.
			subtract_minute();
			set_mode_calibrate_zero_scale();
		}
		else
		{
			add_hour();
			show_time();
		}
		break;
		
	case METER_MODE_CALIBRATE_ZERO_SCALE:
		if( s2_active ) set_mode_calibrate_full_scale();
		break;
		
	case METER_MODE_CALIBRATE_FULL_SCALE:
		if( s2_active ) set_mode_show_time();
		break;
		
	default:
		break;
	}
}

inline void s1_released(void)
{
}

inline void s2_pressed(void)
{
	switch( meter_mode )
	{
	case METER_MODE_SHOW_TIME:
		if( s1_active )
		{
			// Undo hour increment. We wound up incrementing
			// it on the way in to calibration mode, before the
			// second button was held down.
			subtract_hour();
			set_mode_calibrate_zero_scale();
		}
		else
		{
			add_minute();
			// TODO: It was once true that when you released
			// the minute set button (S2), it would reset the
			// second counter, allowing the user to precisely
			// set the "second hand" of the clock. That doesn't
			// work so well with the advent of a two-button
			// clock...
			//second = 0;
			show_time();
		}
		break;
		
	case METER_MODE_CALIBRATE_ZERO_SCALE:
		if( s1_active ) set_mode_calibrate_full_scale();
		break;
		
	case METER_MODE_CALIBRATE_FULL_SCALE:
		if( s1_active ) set_mode_show_time();
		break;
		
	default:
		break;
	}
}

inline void s2_released(void)
{
}

#define S1_PORT P1IN
#define S1_BIT BIT0

#define S2_PORT P1IN
#define S2_BIT BIT3

inline void debounce_buttons(void)
{
	// Each switch has a debounce counter.
	// The debounce counter causes us to sample S1 a bunch
	// of times before we consider the button to really be
	// pressed. This is important because most switches
	// don't close cleanly when pressed -- they make noise,
	// which can cause multiple counts. That'd make setting
	// the time rather difficult, if every time you pressed
	// a button you couldn't be sure if the clock would
	// increment one hour or three... Or seven, or four...
	
	if( s1_active )
	{
		// Last time, S1 was pressed.
		if( S1_PORT & S1_BIT )
		{
			// Button is released, mark button as inactive.
			s1_active = 0;
			s1_released();
		}
	}
	else
	{
		// Last time, S1 was not pressed.
		if( (S1_PORT & S1_BIT) == 0 )
		{

			// S1 is now pressed. Increment the debounce counter.
			debounce_counter_s1++;
			
			// S1 has been pressed for a while, it must be intentional.
			if( debounce_counter_s1 == debounce_wait )
			{
				s1_active = 1;
				s1_pressed();
			}
		}
		else
		{
			debounce_counter_s1 = 0;
		}
	}

	if( s2_active )
	{
		// If button is released, mark button as inactive.
		if( S2_PORT & S2_BIT )
		{
			s2_active = 0;
			s2_released();
		}
	}
	else
	{
		if( (S2_PORT & S2_BIT) == 0 )
		{
			debounce_counter_s2++;
			if( debounce_counter_s2 == debounce_wait )
			{
				s2_active = 1;
				s2_pressed();
			}
		}
		else
		{
			debounce_counter_s2 = 0;
		}
	}
}
	
WDT_ISR(Watchdog_ISR) __interrupt void Watchdog_ISR(void)
{
	static const unsigned short second_divider = 512;
	static unsigned short second_divider_count = 1;
	
	// A bit of fancy footwork occurs here. We're using a 32.768KHz
	// quartz crystal as our timebase. And while the MSP430 has two
	// PWM counters, a third would be required to get decent meter
	// resolution (60 ticks) while keeping the PWM cycle (refresh
	// rate) fast enough that the needle doesn't jiggle or vibrate.
	
	// What we've done is used the real-time clock interrupt to act
	// as our PWM cycle timer. Inside the interrupt, we force the PWM
	// outputs 0/low/off. Then, we schedule the PWM to go high at a
	// point in the future that will give us the correct on-time when
	// the next RTC interrupt occurs.
	
	// Since this interrupt runs in the PWM off-time, it's important
	// that we setup the PWM immediately and that it takes a
	// consistent amount of time. If the setup takes a long time, we
	// may be late turning the PWM on if PWM duty cycle is high (in
	// other words, the PWM output is 1/high/on most of the time).
	// Or, if the setup code is inconsistent, the meter needles will
	// dance around a bit.
	
	// Once the PWM's on-time is set, we have time to relax and do
	// user-interface and housekeeping stuff. Once that work is done,
	// we exit the interrupt. Upon leaving the interrupt, the MSP430
	// restores its prior power state -- going to sleep to save its
	// batteries.
	
	// Reset timer count.
    TACTL	= TASSEL_1 |	// source: ACLK/VLO
    		  ID_0 |		// divide by: 1 (no-op)
    		  MC_2 |		// mode: continuous
    		  TACLR |		// clear count, divider, direction: yes
    		  0 |			// interrupt: disabled
    		  0;			// interrupt pending: no

	// Force PWM OUT pins low.
	TACCTL0	= CM_0 |		// capture mode: no
			  CCIS_0 |		// capture/compare input: CCI0A (don't care)
			  0 |			// synchronize capture source: asynchronous capture
			  0 |			// capture mode: compare
			  OUTMOD_0 |	// output mode: OUT
			  0 |			// capture/compare interrupt: disabled
			  0 |			// output (mode 0 only): 1
			  0 |			// capture overflow: clear
			  0;			// capture/compare interrupt pending: no
	TACCTL1	= CM_0 |		// capture mode: no
			  CCIS_0 |		// capture/compare input: CCI0A (don't care)
			  0 |			// synchronize capture source: asynchronous capture
			  0 |			// capture mode: compare
			  OUTMOD_0 |	// output mode: OUT
			  0 |			// capture/compare interrupt: disabled
			  0 |			// output (mode 0 only): 1
			  0 |			// capture overflow: clear
			  0;			// capture/compare interrupt pending: no

	// Configure PWM behavior to set OUT high when timer expires.
    TACCTL0 = CM_0 |		// capture mode: no
    		  CCIS_0 |		// capture/compare input: CCI0A (don't care)
    		  0 |			// synchronize capture source: asynchronous capture
    		  0 |			// capture mode: compare
    		  OUTMOD_1 |	// output mode: set (reset on TACLR)
    		  0 |			// capture/compare interrupt: disabled
    		  0 |			// output (mode 0 only): 1
    		  0 |			// capture overflow: clear
    		  0;			// capture/compare interrupt pending: no    
    TACCTL1 = CM_0 |		// capture mode: no
    		  CCIS_0 |		// capture/compare input: CCI1A (don't care)
    		  0 |			// synchronize capture source: asynchronous capture
    		  0 |			// capture mode: compare
    		  OUTMOD_1 |	// output mode: reset (reset on TACLR)
    		  0 |			// capture/compare interrupt: disabled
    		  0 |			// output (mode 0 only): 1
    		  0 |			// capture overflow: clear
    		  0;			// capture/compare interrupt pending: no

	// Set timer durations.
	TACCR1 = timer_count_m;
	TACCR0 = timer_count_h;

	// Update time.
	// Since this interrupt routine is run 512 times a second (by the
	// RTC/watchdog timer), we must count 512 interrupts before
	// incrementing the second.
	second_divider_count--;
	if( second_divider_count == 0 )
	{
		second_divider_count = second_divider;
		tick_second();

		// The seconds, minutes, hours may have changed, so update the
		// timer values (which control the meters). But only update them
		// if we're in "show time" mode.		
		if( meter_mode == METER_MODE_SHOW_TIME )
		{
			show_time();
		}
	}
	
	debounce_buttons();
}

int main(void)
{
	// Configure the watchdog to interrupt every 64 ACLK cycles. ACLK
	// is running at 32.768KHz, so the watchdog interrupt goes off
	// every 1.9 milliseconds, or 512 times a second.
    WDTCTL	= WDT_ADLY_1_9;
	
    P1REN	= 0x09;			// Port 1: bits 0,3 resistors enabled, bits 1,2,4-7 resistors disabled
    P1DIR	= 0xF6;			// Port 1: bits 1,2 are outputs, bits 0,3 are inputs, 4-7 unused (outputs)
    P1SEL	= 0x06;			// Port 1: bits 1,2 are PWM, bits 0,3-7 are GPIO
    P1OUT	= 0x09;			// Port 1: bits 0,3 are pulled up
    P1IES	= 0x00;			// Port 1: interrupt edge selects (don't care)
    P1IE	= 0x00;			// Port 1: no interrupts

    P2REN	= 0x00;			// Port 2: all resistors disabled
    P2DIR	= 0x3F;			// Port 2: bits 0-5 unused (outputs)
    P2SEL	= 0xC0;			// Port 2: bits 6:7 are LFXT1, bits 0:5 are GPIO
    P2OUT	= 0x00;			// Port 2: don't care
    P2IES	= 0x00;			// Port 2: interrupt edge selects (don't care)
    P2IE	= 0x00;			// Port 2: no interrupts

	// There's a trade-off here between running the processor slowly
	// or quickly. Run it slowly, and it requires very little current.
	// But if you have a lot of work for the processor to do, it will
	// get it done very slowly. If you run the processor quickly, it
	// requires more current, but finishes in a fraction of the time.
	// Average power is all we care about.
	
	// How about some hard data?
	// Referring to the MSP430x20x1/2/3 datasheet, figure 3...
	//
	// At 3V,  1MHz:   300uA or   900uW, 900uW/MHz
	//         8MHz: 1,900uA or 5,700uW, 712uW/MHz
	//        12MHz: 2,800uA or 8,400uW, 700uW/MHz
	//
	// Notice that the processor gets more efficient per clock cycle as
	// it runs faster. So we'll save power by running the processor
	// faster. This assumes we can get the job done in the same number
	// of clock cycles, regardless of how fast the processor is running.
	// For this processor, it's true, but for fancier systems (like PCs),
	// running a processor faster introduces memory wait states, hard
	// drive latency, etc., which makes this speed/power analysis far
	// more difficult.

	// Not so fast though -- the processor will not function if running
	// at a high clock speed and very low voltage. Look at the
	// MSP430x20x1/2/3 datasheet to see the limitations.
	
 	BCSCTL1 = XT2OFF |		// XT2OFF: XT2 oscillator: off
			  0 |			// XTS: LFXT1 mode: low frequency
			  0 |			// DIVAx: ACLK divide by: 1
			  13;			// RSELx: Range select: 13: 6 - 9.6MHz
//			  7;			// RSELx: Range select: 7: 1MHz
//			  5;			// RSELx: Range select: 5: 390 - 770KHz
//			  3;			// RSELx: Range select: 3: 200 - 400KHz

	BCSCTL2 = 0 |			// SELMx: MCLK source: DCOCLK
			  0 |			// DIVMx: MCLK divide by: 1
			  0 |			// SELS: SMCLK source: DCOCLK
			  0 |			// DIVSx: SMCLK divide by: 1
			  0;			// DCOR: DCO resistor: internal

    BCSCTL3	= XT2S_0 |		// XT2Sx: XT2 range: 0.4 - 1MHz crystal or resonator
    		  LFXT1S_0 |	// LFXT1Sx: LF clock select: 32768Hz clock
    		  XCAP_3 |		// XCAPx: Crystal load capacitance: ~12.5pF
    		  0 |			// XT2OF: XT2 oscillator fault: none
    		  0;			// LFXT1OF: LFXT1 oscillator fault: none
    
    IE1		|= WDTIE;

	_enable_interrupts();
	
    while(1)						//main loop, never ends...
    {
    	// Go to sleep and wait for an interrupt.
    	LPM3;
	}
}
