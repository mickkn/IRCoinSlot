/**
 * @file        IRCoinSlot.ino
 *
 * @brief       Code for the IR Coin Slot project, which detects when a coin is inserted
 *              using an infrared sensor and sends a key input via USB.
 *
 * @details     This project uses an infrared sensor to detect the presence of a coin when it is inserted into the slot.
 *              When a coin is detected, the microcontroller sends a predefined key input (e.g., a keyboard key or a
 *              game controller button) via USB to the connected computer. The code includes the necessary setup for the
 *              infrared sensor, the detection logic, and the USB HID functionality to send the key input. The project
 *              is designed to be simple and efficient, allowing for quick response times when a coin is inserted.
 *              It can be used for various applications, such as arcade machines, vending machines, or any interactive
 *              project that requires coin detection and a corresponding action on a computer.
 *
 *              Code based on Arduino examples and USB HID libraries.
 *
 *              It supports option for 2 coin slots via 2 IR sensors, and can be configured to send different key inputs
 *              for each slot. There is also a bebounce function to prevent multiple detections from a single coin
 *              insertion, which can be adjusted based on the expected coin size and speed of insertion.
 *
 *              The sensor sensitivity can be adjusted via a potentiometer connected to an analog input, allowing for
 *              fine-tuning of the detection threshold based on the specific IR sensor and coin characteristics.
 *
 * @date        14-06-2026
 * @author      Mick K
 *
 */


void setup()
{

}

void loop()
{

}