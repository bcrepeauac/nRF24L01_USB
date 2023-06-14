/*
 * See documentation at https://nRF24.github.io/RF24
 * See License information at root directory of this library
 * Author: Brian Crepeau
 * Version 1.0
 */

/**
 * A simple example of sending data from 1 nRF24L01 transceiver to another.
 *
 * This example was written to be used on 2 devices acting as "nodes".
 * Use the Serial Monitor to change each node's behavior.
 */
#include <SPI.h>
#include "printf.h"
#include "RF24.h"

// Pin defines for the nRF24
#define CE 7
#define CSN 8

//Interpreter Settings
#define MAXPAYLOAD 32 //Maximum number of bytes that can be transmitted in one message
#define TERMCHAR 0x0A // Termination character
#define CMDCHAR '+' // Control character
// Time to wait for a new character before transmitting whatever is left in the buffer, in milliseconds
#define TIMEOUT 500 //0.5 Seconds
#define ERROR_LED PD5
#define STATUS_LED PD6 


// instantiate an object for the nRF24L01 transceiver
RF24 radio(CE, CSN);  // using pin 7 for the CE pin, and pin 8 for the CSN pin

// Write adress, default is 0xC2C2C2C2C2 Page 57 of nRF24L01 manual
uint8_t tx_address[6] = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2}; 
// Read address, this is set to be the same as the TX address so the 
// units can be used with default programming and transmit between two nodes
uint8_t rx_address[6] = {0xC2, 0xC2, 0xC2, 0xC2, 0xC2};

//  Variable for tracking parsing errors
bool globalErrorFlag = false;

// Buffer to hold received data, 32 bytes max plus headroom
char payload[34] = {0};

// Used to hold data transmitted by the UART
// Can hold up to 96 characters (3 full packets)
char data[96];

// Index for serial data array
static int index = 0; 

// Command response defines
#define OK Serial.println("OK")
#define FAIL Serial.println("FAIL")

unsigned char processInt(unsigned char x, unsigned char y, unsigned char z){
  // Converts three consecutive decimal values into a single char
  // IE 1, 2, 3 -> 123
  if (x < 0x30 || x > 0x39 ){
    //0xFF indicates input is out of range
    return 0xFF;
  }
  //Serial.println("X ok");
  if (y < 0x30 || y > 0x39 ){
    //0xFF indicates input is out of range
    return 0xFF;
  }
  //Serial.println("Y ok");
  if (z < 0x30 || z > 0x39 ){
    //0xFF indicates input is out of range
    return 0xFF;
  }
  //Serial.println("Z ok");
  //Serial.println((x-0x30)*100 + (y - 0x30)*10 + (z - 0x30));
  return (x-0x30)*100 + (y - 0x30)*10 + (z - 0x30);
}

unsigned char processHex(unsigned char msb, unsigned char lsb){
  // Converts a pair of hex characters into the corresponding value
  // IE A, 5 -> 0xA5

  globalErrorFlag = false;
  //Serial.print("msb: ");
  //Serial.println(msb, HEX);
  //Serial.print("lsb: ");
  //Serial.println(lsb, HEX);
  if (msb < 0x30 || msb > 0x46 || lsb < 0x30 || lsb > 0x46){
    return 0xFF;  // If the char is less than 0 or more than F, return with an error
    globalErrorFlag = true;
  }
  if ( msb <= 0x39 ){
    msb = (msb - 0x30); // Turns msb into plain hex
  }
  else{
    msb = (msb - 0x37);
  }
  if ( lsb <= 0x39 ){
    lsb = (lsb - 0x30); // Turns lsb into plain hex
  }
  else{
    lsb = (lsb - 0x37);
  }
  return (msb*0x10)+lsb;

}

void parseCommand(char * data){
  //  Compares the data buffer to accepted commands, calling the appropriate helper functions
  // Needs a pointer to the data buffer for serial data
  // First character is an A if we made it here
  // TODO: refactor to better handle the first character after the control sequence
  data[0] = 'A';
  int index = 1;
  Serial.println("CMD");
  while (true){
    //loop until the exit command is received:
    while (Serial.available()){
      //While data is available
      char c = Serial.read();  //process the incoming char
      data[index] = c;
      index ++;
      if ( c == TERMCHAR ){
        data[index] = 0;  // Replace termchar with a null terminator
        if ( strncmp(data, "AT+CH ", 6) == 0 ){
          // Changes the RF channel to the decimal value in character position
          // 6 through 8.  Range is 0 to 125.  RF Frequency is 2400MHz + <channel number>

          // A T + C H _ x y z
          // 0 1 2 3 4 5 6 7 8
          //             ^
          char *p = data+6; // Pointer to where the channel number starts
          unsigned char chan = processInt(p[0], p[1], p[2]);
          if (chan > 125){
            //Report failure if not a valid number or if the requested channel is more than 125
            FAIL; 
            digitalWrite(ERROR_LED, HIGH);
          }
          radio.setChannel(chan);
          OK;                    
        }
        else if ( strncmp(data, "AT+ADD ", 7) == 0){
          // Sets the write and read address to the 5 byte Hex string in character
          // positions 0x7 through 0x10
          char *p = data + 7;
          // A T + A D D _ [1][2][3][4][5][6][7][8][9][A]
          // 0 1 2 3 4 5 6  7  8  9  A  B  C  D  E  F  10 
          //                ^
          unsigned char address_p[6];
          //Order of bits are reversed since register wants LSB first
          address_p[4] = processHex(p[0], p[1]); // First byte of the address
          address_p[3] = processHex(p[2], p[3]); // Second byte of the address
          address_p[2] = processHex(p[4], p[5]); // Third byte of the address
          address_p[1] = processHex(p[6], p[7]); // Fourth byte of the address
          address_p[0] = processHex(p[8], p[9]); // Fifth byte of the address
          address_p[5] = 0x00; // Null terminate the string
          Serial.print("New Address: ");
          for (int i=0; i < 5; i++){
            Serial.print(address_p[i], HEX);
          }
          Serial.println();
          if (globalErrorFlag){
            //If an error occured during the processing of the hex, don't change the address
            FAIL;
            digitalWrite(ERROR_LED, HIGH);
          }
          //If the processing is successful, copy the string into the global address
          strcpy(rx_address, address_p);
          strcpy(tx_address, address_p);
          radio.closeReadingPipe(1); // Close the reading pipe before changing the address
          radio.openReadingPipe(1, rx_address);  //Reopen with the new address
          radio.openReadingPipe(0, tx_address);
          radio.openWritingPipe(tx_address); //Changes the TX Address
          radio.startListening();  // Start listening again
          OK;
        }
        else if ( strncmp(data, "AT+RADD ", 8) == 0){
          // Sets the RX address to the 5 byte Hex string in character
          // positions 0x8 through 0x11
          char *p = data + 8;
          // A T + R A D D _ [1][2][3][4][5][6][7][8][9][A]
          // 0 1 2 3 4 5 6 7  8  9  A  B  C  D  E  F  10 11
          //                  ^
          unsigned char address_p[6];
          //Order of bits are reversed since register wants LSB first
          address_p[4] = processHex(p[0], p[1]); // First byte of the address
          address_p[3] = processHex(p[2], p[3]); // Second byte of the address
          address_p[2] = processHex(p[4], p[5]); // Third byte of the address
          address_p[1] = processHex(p[6], p[7]); // Fourth byte of the address
          address_p[0] = processHex(p[8], p[9]); // Fifth byte of the address
          address_p[5] = 0x00; // Null terminate the string
          if (globalErrorFlag){
            //If an error occured during the processing of the hex, don't change the address
            FAIL;
            digitalWrite(ERROR_LED, HIGH);
          }
          //If the processing is successful, copy the string into the global address
          strcpy(rx_address, address_p);
          radio.closeReadingPipe(1); // Close the reading pipe before changing the address
          radio.openReadingPipe(1, rx_address);  //Reopen with the new address
          radio.startListening();  // Start listening again
          OK;
        }
        else if ( strncmp(data, "AT+TADD ", 8) == 0){
          // Sets the TX address to the 5 byte Hex string in character
          // positions 0x8 through 0x11
          char *p = data + 8;
          // A T + T A D D _ [1][2][3][4][5][6][7][8][9][A]
          // 0 1 2 3 4 5 6 7  8  9  A  B  C  D  E  F  10 11
          //                  ^
          unsigned char address_p[6];
          //Order of bits are reversed since register wants LSB first
          address_p[4] = processHex(p[0], p[1]); // First byte of the address
          address_p[3] = processHex(p[2], p[3]); // Second byte of the address
          address_p[2] = processHex(p[4], p[5]); // Third byte of the address
          address_p[1] = processHex(p[6], p[7]); // Fourth byte of the address
          address_p[0] = processHex(p[8], p[9]); // Fifth byte of the address
          address_p[5] = 0x00; // Null terminate the string
          if (globalErrorFlag){
            //If an error occured during the processing of the hex, don't change the address
            FAIL;
            digitalWrite(ERROR_LED, HIGH);
          }
          //If the processing is successful, copy the string into the global address
          strcpy(tx_address, address_p);
          radio.closeReadingPipe(1); // Close the reading pipe before changing the address
          radio.openWritingPipe(tx_address); //Re-open the writing channel with new address
          radio.openReadingPipe(1, rx_address);  //Reopen with the old address
          radio.openReadingPipe(0, tx_address);
          radio.startListening();  // Start listening again
          OK;
        }
        else if ( strncmp(data, "AT+STATUS", 9) == 0){
          printf_begin();
          radio.printPrettyDetails(); // Prints relevant information about the radio
        }
        else if ( strncmp(data, "AT0", 3) == 0 ){
          OK;
          //Serial.println("DATA");
          //return to normal operation
          return;
        }

        //Serial.print(strcmp(data, "AT0"));
        //Reset the index to 0 to receive next message
        index = 0;
      }
    }
  }
}

void txData(void){
  // Transmits the data in data buffer over the radio
  char packets = index / MAXPAYLOAD;
  char last_packet_size = index % MAXPAYLOAD;
  bool report = true;
  // Switch to TX mode
  radio.stopListening();
  // Transmit data
  for ( int i=0; i < packets; i++){
    //Serial.println(F("Printing in loop"));
    report = report && radio.write(data+(MAXPAYLOAD*i), MAXPAYLOAD);  // Transmit the full packets
  }
  // Finally transmit partial packet, if it exists
  if ( last_packet_size != 0){
    //radio.setPayloadSize(last_packet_size);  // Temporarily reduce the packet size
    //Serial.println(F("Printing remainder"));
    report = report && radio.write(data+(MAXPAYLOAD*packets), last_packet_size);
    //radio.setPayloadSize(MAXPAYLOAD); // Reset to the max
  }
  // Check if packets sent correctly
  if (!report){
    // An error occurred and the host uC should be notified
    digitalWrite(ERROR_LED, HIGH);
    Serial.println(F("Packet transmission failed."));
    printf_begin();
    radio.printPrettyDetails(); // (larger) function that prints human readable data
  }
  

  // Switch back to RX mode
  radio.startListening();
  // Reset index
  index = 0;
}

void setup() {

  Serial.begin(115200);
  while (!Serial) {
    // some boards need to wait to ensure access to serial over USB
  }

  // initialize the transceiver on the SPI bus
  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (1) {}  // hold in infinite loop
  }

  // print example's introductory prompt
  Serial.println(F("RF24/examples/GettingStarted"));

  // To set the radioNumber via the Serial monitor on startup
  //Serial.println(F("Which radio is this? Enter '0' or '1'. Defaults to '0'"));
  //while (!Serial.available()) {
    // wait for user input
  //}
  //char input = Serial.parseInt();
  //radioNumber = input == 1;
  //Serial.print(F("radioNumber = "));
  //Serial.println((int)radioNumber);

  // role variable is hardcoded to RX behavior, inform the user of this
  //Serial.println(F("*** PRESS 'T' to begin transmitting to the other node"));

  // Set the PA Level low to try preventing power supply related problems
  // because these examples are likely run with nodes in close proximity to
  // each other.
  radio.setPALevel(RF24_PA_LOW);  // RF24_PA_MAX is default.

  // save on transmission time by setting the radio to only transmit the
  // number of bytes we need to transmit a float
  radio.setPayloadSize(MAXPAYLOAD);  // float datatype occupies 4 bytes

  // set the TX address of the RX node into the TX pipe
  radio.openWritingPipe(tx_address);  // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(1, rx_address);  // using pipe 1
  radio.openReadingPipe(0, tx_address);

  // additional setup specific to the node's role
  radio.startListening();  // put radio in RX mode


  //radio.enableDynamicPayloads();

  // For debugging info
  // printf_begin();             // needed only once for printing details
  // radio.printDetails();       // (smaller) function that prints raw register values
  // radio.printPrettyDetails(); // (larger) function that prints human readable data

  pinMode(STATUS_LED, OUTPUT);
  pinMode(ERROR_LED, OUTPUT);

}  // setup

void loop() {


  static unsigned long cmd_time;  //Stopwatch variable
  static unsigned long serial_time; // Time of last char received by serial port

  uint8_t pipe;
  if (radio.available(&pipe)) {              // is there a payload? get the pipe number that recieved it
    //Serial.print(F("Received "));
    uint8_t bytes = radio.getPayloadSize();  // get the size of the payload
    radio.read(&payload, bytes);             // fetch payload from FIFO
    payload[bytes+1] = '\0';
    //Serial.print(bytes);  // print the size of the payload
    //Serial.print(F(" bytes on pipe "));
    //Serial.print(pipe);  // print the pipe number
    //Serial.print(F(": "));
    Serial.print(payload);  // print the payload's value
  }


  if (Serial.available()) {
    serial_time = millis();  //update the last time a char was received

    // variable to keep track of how many command characters have been received
    static int command_count = 0;
    static bool command_mode = false;

    char c = Serial.read();
    data[index] = c;
    index++; // Move pointer to next location

    if (command_mode){
      if (millis() - cmd_time >= 1000 ){ // At least one second has elapsed 
      //since the command escape sequence was received
        // Run function to process the AT Commands        
        if ( c == 'A'){  // If the next char isn't an A it is probably not meant to be a command message
          //Serial.println("Entering Command Mode");
          parseCommand(data);
          index = 0;
          memset(data, '\0', sizeof(data)); //Clear the array of any stale data
          // Purge any data from the buffer since it will be used by the command parsing function
        }          
      }
      command_mode = false; //reset command flag
      command_count = 0;  // reset command count
    }


    if ( c == CMDCHAR ){
      // If we get the CMDCHAR 3 times in a row, enter control mode
      command_count++;
      //Serial.print("Got command ");
      //Serial.print(command_count, DEC);
      //Serial.println(" times.");
      if (command_count == 3){
        // listen for AT Commands
        cmd_time = millis();  //Keep track of the current time

        command_mode = true;
        
      }
    }    
    else{
      //
      command_count = 0;
    }
    
    if ( c == TERMCHAR || index >= MAXPAYLOAD){  
      // If the end of sequence char is received or of a full packet is available
      // Packetize data if needed
      txData();

    }
    else{
      
    }
  }

  if (index != 0){
    //If there is data in the queue
    if (millis() - serial_time > TIMEOUT ){
      txData(); // Transmit whatever is in the buffer if the timeout expires
    }
  }
  static unsigned long hb_timer = 0;
  if (millis() - hb_timer >= 2000){
    hb_timer = millis();
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED)); // Toggle status pin
  }
}  // loop
