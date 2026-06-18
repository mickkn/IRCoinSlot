/**
 * @file        IRCoinSlot.ino
 *
 * @brief       Code for the IR Coin Slot project, which detects when a coin is inserted using an infrared sensor
 *              and sends a key input via USB. Other than that it handles the start button with built-in LED.
 *              Both Player 1 and Player 2 is supported.
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
 *              When a coin is inserted and detected the start button LED will start dimming to indicate that
 *              the coin has been accepted, and will return to full brightness after a short delay.
 *
 * @date        14-06-2026
 * @author      Mick K
 *
 */

#include <Keyboard.h>

#define P1_LED 10               //BUTTON LED PWM ENABLED
#define P1_BUT 11               //BUTTON INPUT ACTIVE LOW
#define P1_COIN A1              //ANALOG INPUT
#define P1_COIN_BUT 2           //SECONDARY COIN BUTTON
#define P1_COIN_LED 12          //IR-LED POWER (could be VCC)
#define P1_SENS A0              //SENSITIVITY POTENTIOMETER ANALOG INPUT
#define P1_KEY_START "1"        //KEY TO SEND WHEN START BUTTON IS PRESSED
#define P1_KEY_CREDIT "5"       //KEY TO SEND WHEN COIN IS DETECTED

#define P2_LED 9                //BUTTON LED PWM ENABLED
#define P2_BUT 8                //BUTTON INPUT ACTIVE LOW
#define P2_COIN A2              //ANALOG INPUT
#define P2_COIN_BUT 6           //SECONDARY COIN BUTTON
#define P2_COIN_LED 7           //IR-LED POWER (could be VCC)
#define P2_SENS A3              //SENSITIVITY POTENTIOMETER ANALOG INPUT
#define P2_KEY_START "2"        //KEY TO SEND WHEN START BUTTON IS PRESSED
#define P2_KEY_CREDIT "6"       //KEY TO SEND WHEN COIN IS DETECTED

#define DIM_DELAY           5   //DIMMER SPEED - LED PWM UPDATE INTERVAL IN MS
#define COIN_DEBOUNCE_DELAY 50  //DEBOUNCE DELAY IN MS

constexpr uint8_t  k_led_full        = 255; ///< Full button LED brightness (PWM 0-255).
constexpr uint8_t  k_led_min         = 20;  ///< Minimum LED brightness at the trough of the coin animation.
constexpr uint16_t k_dim_duration_ms = 500; ///< Total duration of the LED dim-and-recover animation in ms.
constexpr uint16_t k_but_debounce_ms = 50;  ///< Start button debounce delay in ms.
constexpr uint16_t k_thresh_lo       = 200; ///< Lowest possible coin-sensor threshold (ADC units).
constexpr uint16_t k_thresh_hi       = 800; ///< Highest possible coin-sensor threshold (ADC units).


// ============================================================
// Types
// ============================================================

/**
 * @brief Holds all runtime state for one player's coin slot and start button.
 */
struct PlayerState
{
    uint8_t     m_led_pin;          ///< PWM output pin for the start button LED.
    uint8_t     m_but_pin;          ///< Digital input pin for the start button (active LOW).
    uint8_t     m_coin_pin;         ///< Analog input pin for the IR coin sensor.
    uint8_t     m_coin_but_pin;     ///< Digital input pin for the manual coin button (active LOW).
    uint8_t     m_coin_led_pin;     ///< Digital output pin that supplies power to the IR emitter.
    uint8_t     m_sens_pin;         ///< Analog input pin for this player's sensitivity potentiometer.
    const char* m_key_start_ptr;    ///< Keyboard string sent when the start button is pressed.
    const char* m_key_credit_ptr;   ///< Keyboard string sent when a coin is detected.

    uint32_t    m_last_coin_ms;         ///< Timestamp of the last accepted coin detection (ms).
    uint32_t    m_last_but_ms;          ///< Timestamp of the last start-button press (ms).
    uint32_t    m_last_coin_but_ms;     ///< Timestamp of the last manual coin-button press (ms).
    uint32_t    m_dim_start_ms;         ///< Timestamp when the current LED animation began (ms).
    uint32_t    m_last_led_update_ms;   ///< Timestamp of the most recent LED PWM write (ms).
    bool        m_coin_active;          ///< True while the sensor currently reads above the detection threshold.
    bool        m_but_prev;             ///< Previous start-button state; true = not pressed (INPUT_PULLUP high).
    bool        m_coin_but_prev;        ///< Previous manual coin-button state; true = not pressed.
    bool        m_is_dimming;           ///< True while a coin-insert LED animation is in progress.
};


// ============================================================
// File-scope state
// ============================================================

static PlayerState s_p1_state; ///< Runtime state for Player 1.
static PlayerState s_p2_state; ///< Runtime state for Player 2.


// ============================================================
// Forward declarations
// ============================================================

static void     init_player(PlayerState& player_state, uint8_t led_pin, uint8_t but_pin,
                             uint8_t coin_pin, uint8_t coin_but_pin, uint8_t coin_led_pin,
                             uint8_t sens_pin, const char* key_start_ptr, const char* key_credit_ptr);
static void     trigger_credit(PlayerState& player_state);
static uint16_t read_threshold(const PlayerState& player_state);
static void     update_coin(PlayerState& player_state);
static void     update_button(PlayerState& player_state);
static void     update_coin_button(PlayerState& player_state);
static void     update_led(PlayerState& player_state);


// ============================================================
// Implementation
// ============================================================

/**
 * @brief Read a player's sensitivity potentiometer and return a scaled coin-detection threshold.
 * @param player_state Reference to the player whose potentiometer is sampled.
 * @return uint16_t Threshold in ADC units, linearly mapped to [k_thresh_lo, k_thresh_hi].
 */
static uint16_t read_threshold(const PlayerState& player_state)
{
    uint16_t pot_val = static_cast<uint16_t>(analogRead(player_state.m_sens_pin));
    return static_cast<uint16_t>(map(pot_val, 0, 1023, k_thresh_lo, k_thresh_hi));
}


/**
 * @brief Send the credit key and start the LED dim-and-recover animation.
 * @param player_state Reference to the player state to modify.
 */
static void trigger_credit(PlayerState& player_state)
{
    Keyboard.print(player_state.m_key_credit_ptr);
    player_state.m_dim_start_ms = millis();
    player_state.m_is_dimming   = true;
}


/**
 * @brief Initialise a PlayerState struct and configure all associated hardware pins.
 *
 * @param player_state   Output: the PlayerState struct to initialise.
 * @param led_pin        PWM output pin for the start button LED.
 * @param but_pin        Digital input pin for the start button (active LOW).
 * @param coin_pin       Analog input pin for the IR coin sensor.
 * @param coin_but_pin   Digital input pin for the manual coin button (active LOW).
 * @param coin_led_pin   Digital output pin controlling IR emitter power.
 * @param sens_pin       Analog input pin for this player's sensitivity potentiometer.
 * @param key_start_ptr  Keyboard string to send when the start button is pressed.
 * @param key_credit_ptr Keyboard string to send when a coin is detected.
 */
static void init_player(PlayerState&  player_state,
                         uint8_t       led_pin,
                         uint8_t       but_pin,
                         uint8_t       coin_pin,
                         uint8_t       coin_but_pin,
                         uint8_t       coin_led_pin,
                         uint8_t       sens_pin,
                         const char*   key_start_ptr,
                         const char*   key_credit_ptr)
{
    player_state.m_led_pin        = led_pin;
    player_state.m_but_pin        = but_pin;
    player_state.m_coin_pin       = coin_pin;
    player_state.m_coin_but_pin   = coin_but_pin;
    player_state.m_coin_led_pin   = coin_led_pin;
    player_state.m_sens_pin       = sens_pin;
    player_state.m_key_start_ptr  = key_start_ptr;
    player_state.m_key_credit_ptr = key_credit_ptr;

    player_state.m_last_coin_ms       = 0U;
    player_state.m_last_but_ms        = 0U;
    player_state.m_last_coin_but_ms   = 0U;
    player_state.m_dim_start_ms       = 0U;
    player_state.m_last_led_update_ms = 0U;
    player_state.m_coin_active        = false;
    player_state.m_but_prev           = true;
    player_state.m_coin_but_prev      = true;
    player_state.m_is_dimming         = false;

    pinMode(led_pin,      OUTPUT);
    pinMode(but_pin,      INPUT_PULLUP);
    pinMode(coin_but_pin, INPUT_PULLUP);
    pinMode(coin_led_pin, OUTPUT);
    pinMode(sens_pin,     INPUT);

    digitalWrite(coin_led_pin, HIGH);
    analogWrite(led_pin, k_led_full);
}


/**
 * @brief Poll the IR coin sensor and fire a credit event on each rising-edge detection.
 *
 * A coin is accepted once per insertion (rising edge only). COIN_DEBOUNCE_DELAY guards
 * against re-triggering caused by mechanical bounce or slow coin travel.
 *
 * @param player_state Reference to the player state.
 */
static void update_coin(PlayerState& player_state)
{
    uint16_t threshold  = read_threshold(player_state);
    uint16_t sensor_val = static_cast<uint16_t>(analogRead(player_state.m_coin_pin));
    uint32_t now        = millis();
    bool     coin_now   = (sensor_val > threshold);

    if(true == coin_now && false == player_state.m_coin_active)
    {
        if((now - player_state.m_last_coin_ms) >= static_cast<uint32_t>(COIN_DEBOUNCE_DELAY))
        {
            trigger_credit(player_state);
            player_state.m_last_coin_ms = now;
        }
    }

    player_state.m_coin_active = coin_now;
}


/**
 * @brief Poll the start button and send the start key on a falling-edge (button press) event.
 *
 * @param player_state Reference to the player state.
 */
static void update_button(PlayerState& player_state)
{
    bool     but_now = static_cast<bool>(digitalRead(player_state.m_but_pin));
    uint32_t now     = millis();

    if(false == but_now && true == player_state.m_but_prev)
    {
        if((now - player_state.m_last_but_ms) >= static_cast<uint32_t>(k_but_debounce_ms))
        {
            Keyboard.print(player_state.m_key_start_ptr);
            player_state.m_last_but_ms = now;
        }
    }

    player_state.m_but_prev = but_now;
}


/**
 * @brief Poll the manual coin button and fire a credit event on a falling-edge (button press).
 *
 * @param player_state Reference to the player state.
 */
static void update_coin_button(PlayerState& player_state)
{
    bool     but_now = static_cast<bool>(digitalRead(player_state.m_coin_but_pin));
    uint32_t now     = millis();

    if(false == but_now && true == player_state.m_coin_but_prev)
    {
        if((now - player_state.m_last_coin_but_ms) >= static_cast<uint32_t>(k_but_debounce_ms))
        {
            trigger_credit(player_state);
            player_state.m_last_coin_but_ms = now;
        }
    }

    player_state.m_coin_but_prev = but_now;
}


/**
 * @brief Drive the start button LED, executing a smooth dim-and-recover animation on coin insert.
 *
 * Phase 1 (first half of k_dim_duration_ms): fades from k_led_full down to k_led_min.
 * Phase 2 (second half): recovers from k_led_min back to k_led_full.
 * Updates are throttled to one PWM write per DIM_DELAY ms.
 *
 * @param player_state Reference to the player state.
 */
static void update_led(PlayerState& player_state)
{
    uint32_t now = millis();

    if((now - player_state.m_last_led_update_ms) < static_cast<uint32_t>(DIM_DELAY))
    {
        return;
    }

    player_state.m_last_led_update_ms = now;

    if(false == player_state.m_is_dimming)
    {
        return;
    }

    uint32_t elapsed  = now - player_state.m_dim_start_ms;
    uint32_t half_dur = static_cast<uint32_t>(k_dim_duration_ms) / 2U;
    uint8_t  brightness;

    if(elapsed < half_dur)
    {
        brightness = static_cast<uint8_t>(map(static_cast<long>(elapsed), 0L,
                                              static_cast<long>(half_dur),
                                              k_led_full, k_led_min));
    }
    else if(elapsed < static_cast<uint32_t>(k_dim_duration_ms))
    {
        brightness = static_cast<uint8_t>(map(static_cast<long>(elapsed),
                                              static_cast<long>(half_dur),
                                              static_cast<long>(k_dim_duration_ms),
                                              k_led_min, k_led_full));
    }
    else
    {
        brightness = k_led_full;
        player_state.m_is_dimming = false;
    }

    analogWrite(player_state.m_led_pin, brightness);
}


// ============================================================
// Arduino entry points
// ============================================================

/**
 * @brief Initialise USB keyboard HID and configure all hardware peripherals for both players.
 */
void setup()
{
    Keyboard.begin();
    analogReference(DEFAULT);

    init_player(s_p1_state, P1_LED, P1_BUT, P1_COIN, P1_COIN_BUT, P1_COIN_LED,
                P1_SENS, P1_KEY_START, P1_KEY_CREDIT);
    init_player(s_p2_state, P2_LED, P2_BUT, P2_COIN, P2_COIN_BUT, P2_COIN_LED,
                P2_SENS, P2_KEY_START, P2_KEY_CREDIT);
}


/**
 * @brief Main loop: poll all inputs and update all outputs for both players on every iteration.
 */
void loop()
{
    update_coin(s_p1_state);
    update_coin(s_p2_state);
    update_button(s_p1_state);
    update_button(s_p2_state);
    update_coin_button(s_p1_state);
    update_coin_button(s_p2_state);
    update_led(s_p1_state);
    update_led(s_p2_state);
}
