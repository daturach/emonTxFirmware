/*
emonTx V3 CT1234 + 3-phase Voltage example
FOR RFM12B Radio Module O N L Y 
(not RFu238 and not RFM69CW)

An example sketch for the emontx module for
3-phase electricity monitoring, with 4 current transformers 

and 1 only voltage transformer

Part of the openenergymonitor.org project
Licence: GNU GPL V3

Authors: Glyn Hudson, Trystan Lea
Builds upon JeeLabs RF12 library and Arduino
Extended for 3-phase operation: Robert Wall
V.1  7/11/2013    Derived from emonTx_CT123_3Phase_Voltage.ino 
V.2  28/1/2015	  Altered to use low-pass filter and subtract the offset, to remove filter settling time.

emonTx V3 Shield documentation:http://openenergymonitor.org/emon/modules/emonTxV3
emonTx firmware code explanation: http://openenergymonitor.org/emon/modules/emontx/firmware
emonTx / emonTx Shield calibration instructions: http://openenergymonitor.org/emon/buildingblocks/calibration

REQUIRES in [Arduino]/libraries
Arduino.h
WProgram.h
avr/wdt.h                                        // the UNO bootloader 

Does NOT require JeeLib

(does NOT require EmonLib)
=============================================================================================

Extended to allow the voltage measurement of a single phase to be used to generate approximate indications of
power (real and apparent) and phase angle for the other two phases of a 3-phase installation.

NOTE: This sketch is for  4-wire connection at 50 Hz, measuring voltage Line-Neutral, and assuming CT1 - 3 current
measurements are on the incoming lines, and CT4 is on a load/infeed connected line-neutral.
A single AC-AC adapter is required and must be connected between L1 and N.

The measured voltage of phase one is used immediately for the calculations for its own phase, and recorded in an 
array and retrieved later to be in the calculations for the remaining phases.

N.B. "Phase shifted" means a small adjustment by interpolation to the voltage waveform to accommodate small 
( < 10 degrees) phase shifts in the transformers etc. "Delayed" means a delay of the voltage samples by 
approx 1/3 or 2/3 cycle.
Without the 4th c.t. in use, this sketch records approx 24 sample sets per cycle at 50 Hz.

POSSIBLE SOURCES OF ERROR
This method is an approximation. It assumes that the voltages of the three phases remain identical and the angles
between the voltage vectors remain accurately 120 degrees apart. The lower the fault level of the supply (i.e. the 
higher the impedance), the greater the change in the true voltage will be as a result of load changes, and therefore
the inaccuracies that result from these approximations will be greater also.
If the mains frequency changes, this will appear as a change in real power and power factor for L2 and more so for L3. 

CALIBRATION
Include the line " #define CT4LINE " if the fourth C.T. is to be used. Adding or removing CT4 drastically changes the 
phase calibration for L2 and L3.

Adjust Vcal = 234.26 so that the correct voltage for L1 is displayed.
Adjust Ical1 = 119.0 so that the correct current for L1 is displayed.
Do the same for Ical2 & Ical3.
Connect a pure resistive load (e.g. a heater) to L1 and adjust Phasecal1 to display a power factor of 1.00.
Do the same for L2 and L3. If it not possible to keep Phasecal within the range 0 - 2, it is permissible to
change "#define PHASE2 8" and/or "#define PHASE3 17". If either of these are changed, both Phasecal2 
& Phasecal3 will need adjusting.

The fourth channel may be used, for example, for a PV input. In that case, define which line CT4 is connected to and
adjust Phasecal4 likewise.

*/

// #define DEBUGGING                             // enable this line to include debugging print statements
#define SERIALPRINT                              // include print statement for commissioning - comment this line to exclude


// to enable 12-bit ADC resolution on Arduino Due, 
// include the following line in main sketch inside setup() function:
//  analogReadResolution(ADC_BITS);
// otherwise will default to 10 bits, as in regular Arduino-based boards.
#if defined(__arm__)
#define ADC_BITS    12
#else
#define ADC_BITS    10
#endif

#define ADC_COUNTS  (1<<ADC_BITS)


#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

//#include <MemoryFree.h>

#define RF12_433MHZ                              // Frequency of RF12B module can be 
                                                 //    RF12_433MHZ, RF12_868MHZ or RF12_915MHZ. 
                                                 //  You should use the one matching the module you have.
												 //  (Note: this is different to the normal OEM definition.)

const int nodeID = 10;                           //  emonTx RFM12B node ID
const int networkGroup = 210;                    //  emonTx RFM12B wireless network group
                                                 //  - needs to be same as emonBase and emonGLCD needs to be same
                                                 //    as emonBase and emonGLCD

const int UNO = 1;                               // Set to 0 if you are not using the UNO bootloader 
                                                 // (i.e using Duemilanove) - All Atmega's shipped from
                                                 // OpenEnergyMonitor come with Arduino Uno bootloader
const byte TIME_BETWEEN_READINGS = 2;            // Time between readings   

#include <avr/wdt.h>                             // the UNO bootloader 
#include <SPI.h>								 // SPI bus for the RFM module
#include <util/crc16.h>                          // Checksum 

// ISR(WDT_vect) { Sleepy::watchdogEvent(); }


//Set Voltage and current input pins
int inPinV = 0;
int inPinI1 = 1;
int inPinI2 = 2;
int inPinI3 = 3;
int inPinI4 = 4;

// Detect CTs in use
bool CT1inUse = false;
bool CT2inUse = false;
bool CT3inUse = false;
bool CT4inUse = false;												 
                                                           
#define CT4LINE 1                                // Set this to 1, 2, or 3 depending on the Line to which the CT4 load is connected.
											     //  The default is 1
												 // DO NOT DEFINE CT4LINE if the 4th CT is not to be used.
												 // The timing values "PHASE2", "PHASE3", "Phasecal2" & "Phasecal3" will be different 
                                                 // depending on whether CT 4 is used or not.
												 
#define PHASE2 6                                 //  Number of samples delay for L2

#define PHASE3 14                                //  Number  of samples delay for L3, also size of array
                                                 //  These can be adjusted if the phase correction is not adequate
                                                 //  Suggested starting values for 3 ct's  [4 ct's]:
                                                 //    PHASE2                         7    [   6  ]
                                                 //    PHASE3                        17    [  14  ]
                                                 //    Phasecal2 =                 0.22    [ 0.60 ]
                                                 //    Phasecal3 =                 0.40    [ 0.08 ]
                                                 //  Suggested starting values for Phasecal4
												 //  On Line 1:  1.10,  Line 2: 0.09,  Line 3: 0.35

//Calibration coefficients
//These need to be set in order to obtain accurate results
double Vcal = 244.6;                             // Calibration constant for voltage input
double Ical1 = 90.9;                             // Calibration constant for current transformer 1
double Ical2 = 90.9;                             // Calibration constant for current transformer 2
double Ical3 = 90.9;                             // Calibration constant for current transformer 3
double Ical4 = 16.6;                             // Calibration constant for current transformer 4

double Phasecal1 = 1.00;                         // Calibration constant for phase shift L1
double Phasecal2 = 1.50;                        // Calibration constant for phase shift L2
double Phasecal3 = 1.40;                         // Calibration constant for phase shift L3
double Phasecal4 = 1.10;                         // Calibration constant for phase shift CT 4


//--------------------------------------------------------------------------------------
// Variable declaration for filters, phase shift, voltages, currents & powers
//--------------------------------------------------------------------------------------
double realPower1,                               // The final data
apparentPower1,
powerFactor1,
Irms1,
realPower2,
apparentPower2,
powerFactor2,
Irms2,
realPower3,
apparentPower3,
powerFactor3,
Irms3,
realPower4,
apparentPower4,
powerFactor4,
Irms4,
Vrms;       

    
typedef struct { int power1, power2, power3, power4, Vrms; } PayloadTX;        // neat way of packaging data for RF comms
                                                 // (Include all the variables that are desired,
                                                 // ensure the same struct is used to receive)

PayloadTX emontx;                                // create an instance

const int LEDpin = 6;                            // On-board emonTx LED 
#define RFMSELPIN 10                             // Pins for the RFM Radio module
#define RFMIRQPIN 2
#define SDOPIN 12


void setup() 
{
Serial.begin(9600);

//Serial.print("Mem=");
//Serial.println(freeMemory());


Serial.println(F("emonTx V3.4 CT1234 Voltage 3 Phase example"));
Serial.println(F("OpenEnergyMonitor.org"));
Serial.print(F("Node: ")); 
Serial.print(nodeID); 

Serial.print(F(" Freq: ")); 
#ifdef RF12_433MHZ
  Serial.print(F("433MHz"));
#elif defined RF12_868MHZ
  Serial.print(F("868MHz"));
#elif defined RF12_915MHZ	
  Serial.print(F("915MHz")); 
#else
  Serial.print(F("Not set"));
#endif

Serial.print(F(" Network: ")); 
Serial.println(networkGroup);

// Set up to drive the Radio Module
pinMode (RFMSELPIN, OUTPUT);
digitalWrite(RFMSELPIN,HIGH);
// start the SPI library:
SPI.begin();
SPI.setBitOrder(MSBFIRST);
SPI.setDataMode(0);
SPI.setClockDivider(SPI_CLOCK_DIV8);
// initialise RFM12
delay(200); // wait for RFM12 POR
rfm_write(0x0000); // clear SPI
#ifdef RF12_868MHZ
  rfm_write(0x80E7); // EL (ena dreg), EF (ena RX FIFO), 868 MHz, 12.0pF 
#endif  
#ifndef RF12_868MHZ
  rfm_write(0x80D7); // EL (ena dreg), EF (ena RX FIFO), 433 MHz, 12.0pF 
#endif  
rfm_write(0x8208); // Turn on crystal,!PA
rfm_write(0xA640); // 433 or 868 MHz exactly
rfm_write(0xC606); // approx 49.2 Kbps, as used by emonTx
//rfm_write(0xC657); // approx 3.918 Kbps, better for long range
rfm_write(0xCC77); // PLL 
rfm_write(0x94A0); // VDI,FAST,134kHz,0dBm,-103dBm 
rfm_write(0xC2AC); // AL,!ml,DIG,DQD4 
rfm_write(0xCA83); // FIFO8,2-SYNC,!ff,DR 
rfm_write(0xCEd2); // SYNC=2DD2
rfm_write(0xC483); // @PWR,NO RSTRIC,!st,!fi,OE,EN 
rfm_write(0x9850); // !mp,90kHz,MAX OUT 
rfm_write(0xE000); // wake up timer - not used 
rfm_write(0xC800); // low duty cycle - not used 
rfm_write(0xC000); // 1.0MHz,2.2V 

pinMode(LEDpin, OUTPUT);                         // Setup indicator LED
digitalWrite(LEDpin, HIGH);

if (analogRead(inPinI1) != 0)
	CT1inUse = true;
if (analogRead(inPinI2) != 0)
	CT2inUse = true;
if (analogRead(inPinI3) != 0)
	CT3inUse = true;
if (analogRead(inPinI4) != 0)
	CT4inUse = true;
}

//*********************************************************************************************************************
void loop() 
{ 
// Outer loop - Reads Voltages & Currents - Sends results
calcVI3Ph(11,2000);                              // Calculate all. No.of complete cycles, time-out  

// Removing these print statements is recommended for normal use (if not required).
#ifdef SERIALPRINT

Serial.print(F("Voltage: ")); Serial.println(Vrms);
Serial.print(F(" Phase 1: ")); Serial.print(Irms1);
Serial.print(F(" A, ")); Serial.print(realPower1);
Serial.print(F(" W, ")); Serial.print(apparentPower1);
Serial.print(F(" VA, PF=")); Serial.println(powerFactor1);
Serial.print(F(" Phase 2: ")); Serial.print(Irms2);
Serial.print(F(" A, ")); Serial.print(realPower2);
Serial.print(F(" W, ")); Serial.print(apparentPower2);
Serial.print(F(" VA, PF=")); Serial.println(powerFactor2);

Serial.print(F(" Phase 3: ")); Serial.print(Irms3);
Serial.print(F(" A, ")); Serial.print(realPower3);
Serial.print(F(" W, ")); Serial.print(apparentPower3);
Serial.print(F(" VA, PF=")); Serial.println(powerFactor3);

#ifdef CT4LINE
Serial.print(F(" Input 4: ")); Serial.print(Irms4);
Serial.print(F(" A, ")); Serial.print(realPower4);
Serial.print(F(" W, ")); Serial.print(apparentPower4);
Serial.print(F(" VA, PF=")); Serial.println(powerFactor4);
#endif

Serial.println(); delay(100);

#endif  // SerialPrint

emontx.power1 = realPower1;                      // Copy the desired variables ready for transmission
emontx.power2 = realPower2;
emontx.power3 = realPower3;
emontx.power4 = realPower4;
emontx.Vrms   = Vrms;

send_rf_data();                                  // *SEND RF DATA* - see emontx_lib
digitalWrite(LEDpin, HIGH); delay(2); digitalWrite(LEDpin, LOW);      // flash LED
delay(TIME_BETWEEN_READINGS*1000);  

}

//*********************************************************************************************************************

void calcVI3Ph(int cycles, int timeout)
{
    //--------------------------------------------------------------------------------------
    // Variable declaration for filters, phase shift, voltages, currents & powers
    //--------------------------------------------------------------------------------------

    int lastSampleV,sampleV;              // 'sample' holds the raw analogue read value, 'lastSample' holds the last sample
    int sampleI1;
    int sampleI2;
    int sampleI3;
    int sampleI4;


    double lastFilteredV,filteredV;       // 'Filtered' is the raw analogue value minus the DC offset
    double filteredI1;
    double filteredI2;
    double filteredI3;
    double filteredI4;

    static double offsetV  = ADC_COUNTS>>1;      //Low-pass filter output - start at half-rail, or zero if CT was not detected.
    static double offsetI1 = CT1inUse?ADC_COUNTS>>1 : 0;
    static double offsetI2 = CT2inUse?ADC_COUNTS>>1 : 0;
    static double offsetI3 = CT3inUse?ADC_COUNTS>>1 : 0;
    static double offsetI4 = CT4inUse?ADC_COUNTS>>1 : 0;
    

    double phaseShiftedV1;                       // Holds the calibrated delayed & phase shifted voltage.
    double phaseShiftedV2;
    double phaseShiftedV3;
    double phaseShiftedV4;

    double sumV,sumI1,sumI2,sumI3,sumI4;
    double sumP1,sumP2,sumP3,sumP4;              // running sum leading to the mean value



    int startV;                                  // Instantaneous voltage at start of sample window.

    int SupplyVoltage = 3300;                    // Hardcode supply voltage for emonTx V3, it should be always 3.3V
    int crossCount = -2;                         // Used to measure number of times threshold is crossed.
    int numberOfSamples = 0;                     // This is now incremented  
    int numberOfPowerSamples = 0;                // Needed because 1 cycle of voltages needs to be stored before use
    boolean lastVCross, checkVCross;             // Used to measure number of times threshold is crossed.
	double storedV[PHASE3];                      // Array to store >240 degrees of voltage samples
	
    //-------------------------------------------------------------------------------------------------------------------------
    // 1) Waits for the waveform to be close to 'zero' (1/2 scale adc) part in sin curve.
    //-------------------------------------------------------------------------------------------------------------------------
    boolean st=false;                            // an indicator to exit the while loop

    unsigned long start = millis();              // millis()-start makes sure it doesn't get stuck in the loop if there is an error.

    while(st==false)                             // Wait for first zero crossing...
    {
        startV = analogRead(inPinV);             // using the voltage waveform
        if ((startV < ((ADC_COUNTS>>1)+(ADC_COUNTS>>3))) && (startV > ((ADC_COUNTS>>1)-(ADC_COUNTS>>3)))) st=true;  //check its within range
        if ((millis()-start)>timeout) st = true;
    }

    //-------------------------------------------------------------------------------------------------------------------------
    // 2) Main measurement loop
    //------------------------------------------------------------------------------------------------------------------------- 
    start = millis(); 

    while ((crossCount < cycles * 2) && ((millis()-start)<timeout)) 
    {
        lastSampleV=sampleV;                     //  Used for digital low pass filter - offset removal
        lastFilteredV = filteredV;

        //-----------------------------------------------------------------------------
        // A) Read in raw voltage and current samples
        //-----------------------------------------------------------------------------
        sampleV = analogRead(inPinV);            // Read in raw voltage signal
        sampleI1 = analogRead(inPinI1);          // Read in raw current signal
        sampleI2 = analogRead(inPinI2);          // Read in raw current signal
        sampleI3 = analogRead(inPinI3);          // Read in raw current signal

#ifdef CT4LINE
        sampleI4 = analogRead(inPinI4);          // Read in raw current signal
#endif

		// Apply digital low pass filter to the voltage input, then store it in a circular buffer
        offsetV = offsetV + ((sampleV-offsetV)/1024);
        filteredV = sampleV - offsetV;
        storedV[numberOfSamples%PHASE3] = filteredV;        // store this voltage sample in circular buffer
		
		// Count the number of zero crossings - the first cycle loads the buffer, otherwise it is not used.

        lastVCross = checkVCross;                     

        checkVCross = (sampleV > startV) ? true : false;
        if (numberOfSamples==1)
            lastVCross = checkVCross;                  
                    
        if (lastVCross != checkVCross)
        {
            crossCount++;
            if (crossCount == 0)                 // Started recording at -2 crossings so that one complete cycle 
            {                                    //   has been stored before accumulating.
                sumV  = 0;
                sumI1 = 0;
                sumI2 = 0;
                sumI3 = 0;
                sumI4 = 0;
                sumP1 = 0;                                    
                sumP2 = 0;
                sumP3 = 0;
                sumP4 = 0;
                numberOfPowerSamples = 0;
            }
        }


 		
        //-----------------------------------------------------------------------------
        // B) Apply digital low pass filters to obtain 2.5V DC offset, 
        //    then subtract it (to centre the 'filtered' wave on 0).
        //-----------------------------------------------------------------------------
        offsetI1 = offsetI1 + ((sampleI1-offsetI1)/1024);
        filteredI1 = sampleI1 - offsetI1;
        offsetI2 = offsetI2 + ((sampleI2-offsetI2)/1024);
        filteredI2 = sampleI2 - offsetI2;
        offsetI3 = offsetI3 + ((sampleI3-offsetI3)/1024);
        filteredI3 = sampleI3 - offsetI3;

#ifdef CT4LINE
        offsetI4 = offsetI4 + ((sampleI4-offsetI4)/1024);
        filteredI4 = sampleI4 - offsetI4;
#endif



        //-----------------------------------------------------------------------------
        // D) Root-mean-square method voltage
        //-----------------------------------------------------------------------------  
        sumV += filteredV * filteredV;           // sum += squared voltage values

        //-----------------------------------------------------------------------------
        // E) Root-mean-square method current
        //-----------------------------------------------------------------------------   
        sumI1 += filteredI1 * filteredI1;        // sum += squared current values
        sumI2 += filteredI2 * filteredI2;
        sumI3 += filteredI3 * filteredI3;

#ifdef CT4LINE
        sumI4 += filteredI4 * filteredI4;
#endif

        //-----------------------------------------------------------------------------
        // F) Phase calibration - for Phase 1: shifts V1 to correct transformer errors
        //    for phases 2 & 3 delays V1 by 120 degrees & 240 degrees respectively 
        //    and shifts for fine adjustment and to correct transformer errors.
        //-----------------------------------------------------------------------------
        phaseShiftedV1 = lastFilteredV + Phasecal1 * (filteredV - lastFilteredV);
		
        phaseShiftedV2 = storedV[(numberOfSamples-PHASE2-1)%PHASE3] 
            + Phasecal2 * (storedV[(numberOfSamples-PHASE2)%PHASE3] 
                         - storedV[(numberOfSamples-PHASE2-1)%PHASE3]);

        phaseShiftedV3 = storedV[(numberOfSamples+1)%PHASE3] 
            + Phasecal3 * (storedV[(numberOfSamples+2)%PHASE3]
                         - storedV[(numberOfSamples+1)%PHASE3]);
						 
#ifdef CT4LINE
		if (CT4LINE == 2)
		{
			phaseShiftedV4 = storedV[(numberOfSamples-PHASE2-1)%PHASE3] 
				+ Phasecal4 * (storedV[(numberOfSamples-PHASE2)%PHASE3] 
                             - storedV[(numberOfSamples-PHASE2-1)%PHASE3]);
	
		}
		else if (CT4LINE == 3)
		{
			phaseShiftedV4 = storedV[(numberOfSamples+1)%PHASE3] 
				+ Phasecal4 * (storedV[(numberOfSamples+2)%PHASE3]
                             - storedV[(numberOfSamples+1)%PHASE3]);
		}
		else
			phaseShiftedV4 = lastFilteredV + Phasecal4 * (filteredV - lastFilteredV);
#endif
        //-----------------------------------------------------------------------------
        // G) Instantaneous power calc
        //-----------------------------------------------------------------------------   
        sumP1 += phaseShiftedV1 * filteredI1;    // Sum += Instantaneous Power
        sumP2 += phaseShiftedV2 * filteredI2;
        sumP3 += phaseShiftedV3 * filteredI3;

#ifdef CT4LINE
        sumP4 += phaseShiftedV4 * filteredI4;
#endif
        
        numberOfPowerSamples++;                  // Count number of times looped for Power averages.
        numberOfSamples++;                       // Count number of times looped.    

    }

    //-------------------------------------------------------------------------------------------------------------------------
    // 3) Post loop calculations
    //------------------------------------------------------------------------------------------------------------------------- 
    //Calculation of the root of the mean of the voltage and current squared (rms)
    //Calibration coefficients applied. 

    double V_Ratio = Vcal *((SupplyVoltage/1000.0) / 1023.0);
    Vrms = V_Ratio * sqrt(sumV / numberOfPowerSamples); 

    double I_Ratio1 = Ical1 *((SupplyVoltage/1000.0) / 1023.0);
    Irms1 = I_Ratio1 * sqrt(sumI1 / numberOfPowerSamples); 

    double I_Ratio2 = Ical2 *((SupplyVoltage/1000.0) / 1023.0);
    Irms2 = I_Ratio2 * sqrt(sumI2 / numberOfPowerSamples); 

    double I_Ratio3 = Ical3 *((SupplyVoltage/1000.0) / 1023.0);
    Irms3 = I_Ratio3 * sqrt(sumI3 / numberOfPowerSamples); 

#ifdef CT4LINE
    double I_Ratio4 = Ical4 *((SupplyVoltage/1000.0) / 1023.0);
    Irms4 = I_Ratio4 * sqrt(sumI4 / numberOfPowerSamples); 
#endif

    //Calculation power values
    realPower1 = V_Ratio * I_Ratio1 * sumP1 / numberOfPowerSamples;
    apparentPower1 = Vrms * Irms1;
    powerFactor1 = realPower1 / apparentPower1;

    realPower2 = V_Ratio * I_Ratio2 * sumP2 / numberOfPowerSamples;
    apparentPower2 = Vrms * Irms2;
    powerFactor2 = realPower2 / apparentPower2;

    realPower3 = V_Ratio * I_Ratio3 * sumP3 / numberOfPowerSamples;
    apparentPower3 = Vrms * Irms3;
    powerFactor3 = realPower3 / apparentPower3;

#ifdef CT4LINE
    realPower4 = V_Ratio * I_Ratio4 * sumP4 / numberOfPowerSamples;
    apparentPower4 = Vrms * Irms4;
    powerFactor4 = realPower4 / apparentPower4;
#else
    realPower4 = 0.0;
    apparentPower4 = 0.0;
    powerFactor4 = 0.0;
#endif

    //Reset accumulators
    sumV = 0;
    sumI1 = 0;
    sumI2 = 0;
    sumI3 = 0;
    sumI4 = 0;
    sumP1 = 0;
    sumP2 = 0;
    sumP3 = 0;
    sumP4 = 0;
    //--------------------------------------------------------------------------------------       

#ifdef DEBUGGING
    // Include these statements for development/debugging only
    
    Serial.print(F("Total Samples: ")); Serial.print(numberOfSamples);
    Serial.print(F(" Power Samples: ")); Serial.print(numberOfPowerSamples);
    Serial.print(F(" Time: ")); Serial.print(millis() - start);
    Serial.print(F(" Crossings: ")); Serial.println(crossCount);

    for (int j=0; j<PHASE3; j++)
    {
        Serial.print(storedV[j]); Serial.print(F(" "));
        Serial.println();
    }
#endif
}

void send_rf_data()
{
  rfm_send((byte *)&emontx,sizeof(emontx));	
}

// write a command to the RFM12
word rfm_write(word cmd)
{
  word result;
  
  digitalWrite(RFMSELPIN,LOW);
  result=(SPI.transfer(cmd>>8)<<8) | SPI.transfer(cmd & 0xff);
  digitalWrite(RFMSELPIN,HIGH);
  return result;
}

// transmit data via the RFM12
void rfm_send(byte *data, byte size)
{
  byte i=0,next,txstate=0;
  word crc=~0;
  
  rfm_write(0x8228); // OPEN PA
  rfm_write(0x8238);

  digitalWrite(RFMSELPIN,LOW);
  SPI.transfer(0xb8); // tx register write command
  
  while(txstate<13)
  {
    while(digitalRead(SDOPIN)==0); // wait for SDO to go high
    switch(txstate)
    {
      case 0:
      case 1:
      case 2: next=0xaa; txstate++; break;
      case 3: next=0x2d; txstate++; break;
      case 4: next=0xd2; txstate++; break;
      case 5: next=10; txstate++; break; // node ID
      case 6: next=size; txstate++; break;
      case 7: next=data[i++]; if(i==size) txstate++; break;
      case 8: next=(byte)crc; txstate++; break;
      case 9: next=(byte)(crc>>8); txstate++; break;
      case 10:
      case 11:
      case 12: next=0xaa; txstate++; break; // dummy bytes (if <3 CRC gets corrupted sometimes)
    }
    if((txstate>4)&&(txstate<9)) crc = _crc16_update(crc, next);
    SPI.transfer(next);
  }
  digitalWrite(RFMSELPIN,HIGH);

  rfm_write( 0x8208 ); // CLOSE PA
  rfm_write( 0x8200 ); // enter sleep
}
