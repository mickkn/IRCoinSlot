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
 *              When a coin is accepted the start button LED repeatedly fades down and up until the
 *              start button is pressed, then turns off.
 *
 * @date        14-06-2026
 * @author      Mick K
 *
 */

#include <Keyboard.h>

#define DEBUG true             //ENABLE DEBUG PRINTS TO SERIAL MONITOR

#define P1_ENABLE true          //ENABLE FIRST PLAYER
#define P1_LED 3                //BUTTON LED PWM ENABLED (TIMER0)
#define P1_BUT 2                //BUTTON INPUT ACTIVE LOW (INT1)
#define P1_COIN_BUT 7           //BUTTON INPUT ACTIVE LOW (SKIPPING COIN INSERT) (INT6)
#define P1_COIN A1              //ANALOG INPUT
#define P1_SENS A0              //SENSITIVITY POTENTIOMETER ANALOG INPUT
#define P1_KEY_START "1"        //KEY TO SEND WHEN START BUTTON IS PRESSED
#define P1_KEY_CREDIT "5"       //KEY TO SEND WHEN COIN IS DETECTED

#define P2_ENABLE true          //ENABLE SECOND PLAYER
#define P2_LED 5                //BUTTON LED PWM ENABLED (TIMER3)
#define P2_BUT 14               //BUTTON INPUT ACTIVE LOW (PCINT3)
#define P2_COIN_BUT 15          //BUTTON INPUT ACTIVE LOW (SKIPPING COIN INSERT) (PCINT1)
#define P2_COIN A2              //ANALOG INPUT
#define P2_SENS A3              //SENSITIVITY POTENTIOMETER ANALOG INPUT
#define P2_KEY_START "2"        //KEY TO SEND WHEN START BUTTON IS PRESSED
#define P2_KEY_CREDIT "6"       //KEY TO SEND WHEN COIN IS DETECTED

#define DIM_DELAY           5   //DIMMER SPEED - LED PWM UPDATE INTERVAL IN MS
#define COIN_DEBOUNCE_DELAY 2000 //ADC COIN COOLDOWN AFTER AN ACCEPTED INSERT, IN MS

constexpr uint8_t  k_led_full          = 255;  ///< Peak button LED brightness during the fade (PWM 0-255).
constexpr uint8_t  k_led_min           = 10;   ///< Minimum fade brightness; keeps PWM active at the trough.
constexpr uint8_t  k_led_off           = 0;    ///< LED off; PWM is disabled on the pin.
constexpr uint16_t k_dim_duration_ms   = 2000; ///< Duration of one repeating LED fade cycle (min -> full -> min) in ms.
constexpr uint16_t k_but_debounce_ms   = 50;   ///< Release time required before a button re-arms, in ms.
constexpr uint16_t k_thresh_lo         = 500;  ///< Lowest possible coin-sensor threshold (ADC units); least sensitive.
constexpr uint16_t k_thresh_hi         = 1020;  ///< Highest possible coin-sensor threshold (ADC units); most sensitive.
constexpr uint16_t k_coin_hysteresis   = 30;   ///< Release threshold offset above the detection threshold (ADC units).
constexpr uint32_t k_debug_interval_ms = 2000; ///< Interval between analog debug readouts.
constexpr uint16_t k_debug_buf_size    = 512;  ///< Size of the non-blocking debug output ring buffer.
constexpr uint8_t  k_max_pcint_buttons = 4;    ///< Maximum number of buttons sharing the PCINT0 vector.


// ============================================================
// Types
// ============================================================

/**
 * @brief Interrupt-latched, active-LOW push button.
 *
 * The ISR latches a press immediately on the falling edge and disarms the button.
 * The main loop consumes the press and re-arms the button once it has read HIGH
 * (released) continuously for k_but_debounce_ms.
 */
struct Button
{
    uint8_t         m_pin;              ///< Digital input pin (INPUT_PULLUP, active LOW).
    volatile bool   m_armed;            ///< True when a new press may be latched.
    volatile bool   m_pending;          ///< True when a press was latched by the ISR and not yet handled.
    uint32_t        m_high_since_ms;    ///< Timestamp when the pin was first seen HIGH while disarmed.
    bool            m_high_seen;        ///< True while the pin has been continuously HIGH since m_high_since_ms.
};

/**
 * @brief Holds all runtime state for one player's coin slot and start button.
 */
struct PlayerState
{
    uint8_t     m_led_pin;          ///< PWM output pin for the start button LED.
    uint8_t     m_coin_pin;         ///< Analog input pin for the IR coin sensor.
    uint8_t     m_sens_pin;         ///< Analog input pin for this player's sensitivity potentiometer.
    const char* m_key_start_ptr;    ///< Keyboard string sent when the start button is pressed.
    const char* m_key_credit_ptr;   ///< Keyboard string sent when a coin is detected.
    uint8_t     m_id;               ///< Player number used in debug output.

    Button      m_start_but;        ///< Start button.
    Button      m_coin_but;         ///< Coin-insert button.

    uint32_t    m_last_coin_ms;         ///< Timestamp of the last accepted coin detection (ms).
    uint32_t    m_dim_start_ms;         ///< Timestamp when the current LED animation began (ms).
    uint32_t    m_last_led_update_ms;   ///< Timestamp of the most recent LED PWM write (ms).
    bool        m_coin_active;          ///< Set when the sensor reads below the detection threshold; cleared at threshold + k_coin_hysteresis or above.
    bool        m_is_dimming;           ///< True while the LED fade is running.
};

/**
 * @brief Print target that queues debug text and sends it to USB Serial without blocking.
 *
 * While no Serial Monitor is connected, text stays queued. When the ring buffer is full,
 * new text is dropped and counted; dropping continues until the queued text has been
 * sent, then a "[N debug bytes dropped]" notice is printed in place of the lost text.
 * drain() writes only as many bytes as the USB CDC endpoint can accept immediately.
 */
class DebugBuffer : public Print
{
public:
    size_t write(uint8_t c) override
    {
        uint16_t next = static_cast<uint16_t>((m_head + 1U) % k_debug_buf_size);
        // Once text is lost, keep dropping until the notice is queued to preserve ordering.
        if(next == m_tail || 0U != m_dropped)
        {
            if(0xFFFFFFFFUL != m_dropped)
            {
                ++m_dropped;
            }
            return 0U;
        }
        m_buf[m_head] = c;
        m_head        = next;
        return 1U;
    }

    void drain()
    {
        // Serial's bool operator contains delay(10); dtr() does not.
        if(false == Serial.dtr())
        {
            return;
        }

        for(;;)
        {
            if(m_tail == m_head)
            {
                if(0U == m_dropped)
                {
                    return;
                }
                uint32_t dropped = m_dropped;
                m_dropped        = 0U;
                print(F("\r\n["));
                print(dropped);
                println(F(" debug bytes dropped]"));
                continue;
            }

            int space = Serial.availableForWrite();
            if(space <= 0)
            {
                return;
            }
            uint16_t chunk = (m_head > m_tail) ? (m_head - m_tail) : (k_debug_buf_size - m_tail);
            if(chunk > static_cast<uint16_t>(space))
            {
                chunk = static_cast<uint16_t>(space);
            }
            Serial.write(&m_buf[m_tail], chunk);
            m_tail = static_cast<uint16_t>((m_tail + chunk) % k_debug_buf_size);
        }
    }

private:
    uint8_t  m_buf[k_debug_buf_size];
    uint16_t m_head    = 0U;
    uint16_t m_tail    = 0U;
    uint32_t m_dropped = 0U;
};


// ============================================================
// File-scope state
// ============================================================

static PlayerState s_p1_state; ///< Runtime state for Player 1.
static PlayerState s_p2_state; ///< Runtime state for Player 2.
static DebugBuffer s_debug;    ///< Non-blocking debug output.

static Button*         s_pcint_buttons[k_max_pcint_buttons]; ///< Buttons served by the PCINT0 vector.
static volatile uint8_t s_pcint_count = 0U;                  ///< Number of entries in s_pcint_buttons.


// ============================================================
// Forward declarations
// ============================================================

static void     init_player(PlayerState& player_state, uint8_t id, uint8_t led_pin, uint8_t but_pin,
                             uint8_t coin_but_pin, uint8_t coin_pin,
                             uint8_t sens_pin, const char* key_start_ptr, const char* key_credit_ptr);
static void     init_button(Button& button, uint8_t pin, void (*isr)());
static void     button_isr(Button& button);
static bool     take_press(Button& button);
static void     rearm_button(Button& button);
static void     trigger_credit(PlayerState& player_state);
static uint16_t read_threshold(const PlayerState& player_state);
static bool     is_coin_present(uint16_t sensor_val, uint16_t threshold, bool coin_active);
static void     update_coin(PlayerState& player_state);
static void     update_buttons(PlayerState& player_state);
static void     update_led(PlayerState& player_state);
static void     debug_event(const PlayerState& player_state, const __FlashStringHelper* message);
static void     debug_analog(const PlayerState& player_state);
static void     update_debug();


// ============================================================
// Interrupt service routines
// ============================================================

static void isr_p1_start() { button_isr(s_p1_state.m_start_but); }
static void isr_p1_coin()  { button_isr(s_p1_state.m_coin_but); }
static void isr_p2_start() { button_isr(s_p2_state.m_start_but); }
static void isr_p2_coin()  { button_isr(s_p2_state.m_coin_but); }

/**
 * @brief Pin-change interrupt for PORTB pins; checks every button registered on it.
 */
ISR(PCINT0_vect)
{
    for(uint8_t i = 0U; i < s_pcint_count; i++)
    {
        button_isr(*s_pcint_buttons[i]);
    }
}


// ============================================================
// Implementation
// ============================================================

/**
 * @brief Latch a press if the button is armed and currently reads LOW. Runs in ISR context.
 * @param button Button to check.
 */
static void button_isr(Button& button)
{
    if(true == button.m_armed && LOW == digitalRead(button.m_pin))
    {
        button.m_armed   = false;
        button.m_pending = true;
    }
}


/**
 * @brief Atomically consume a latched press.
 * @param button Button to check.
 * @return true if a press was pending.
 */
static bool take_press(Button& button)
{
    const uint8_t old_sreg = SREG;
    noInterrupts();
    bool pending     = button.m_pending;
    button.m_pending = false;
    SREG = old_sreg;
    return pending;
}


/**
 * @brief Re-arm a disarmed button once it has been released for k_but_debounce_ms.
 * @param button Button to update.
 */
static void rearm_button(Button& button)
{
    if(true == button.m_armed)
    {
        return;
    }

    uint32_t now = millis();

    if(LOW == digitalRead(button.m_pin))
    {
        button.m_high_seen = false;
        return;
    }

    if(false == button.m_high_seen)
    {
        button.m_high_seen     = true;
        button.m_high_since_ms = now;
        return;
    }

    if((now - button.m_high_since_ms) >= static_cast<uint32_t>(k_but_debounce_ms))
    {
        const uint8_t old_sreg = SREG;
        noInterrupts();
        button.m_armed = true;
        // Catch a press that started between the HIGH read and arming.
        button_isr(button);
        SREG = old_sreg;
        button.m_high_seen = false;
    }
}


/**
 * @brief Configure a button pin and attach its interrupt (external INT or PCINT).
 * @param button Button to initialise.
 * @param pin    Digital input pin.
 * @param isr    ISR used when the pin has an external interrupt.
 */
static void init_button(Button& button, uint8_t pin, void (*isr)())
{
    button.m_pin           = pin;
    button.m_pending       = false;
    button.m_high_seen     = false;
    button.m_high_since_ms = 0U;

    pinMode(pin, INPUT_PULLUP);
    button.m_armed = (HIGH == digitalRead(pin));

    if(NOT_AN_INTERRUPT != digitalPinToInterrupt(pin))
    {
        attachInterrupt(digitalPinToInterrupt(pin), isr, FALLING);
    }
    else if(nullptr != digitalPinToPCICR(pin) && s_pcint_count < k_max_pcint_buttons)
    {
        noInterrupts();
        s_pcint_buttons[s_pcint_count] = &button;
        s_pcint_count                  = static_cast<uint8_t>(s_pcint_count + 1U);
        *digitalPinToPCMSK(pin) |= _BV(digitalPinToPCMSKbit(pin));
        *digitalPinToPCICR(pin) |= _BV(digitalPinToPCICRbit(pin));
        interrupts();
    }
    else
    {
        s_debug.print(F("ERROR: pin "));
        s_debug.print(pin);
        s_debug.println(F(" has no interrupt; button disabled"));
        button.m_armed = false;
    }
}


static void debug_event(const PlayerState& player_state, const __FlashStringHelper* message)
{
    if(DEBUG)
    {
        s_debug.print(millis());
        s_debug.print(F(" ms P"));
        s_debug.print(player_state.m_id);
        s_debug.print(F(": "));
        s_debug.println(message);
    }
}


static void debug_analog(const PlayerState& player_state)
{
    uint16_t pot_val = static_cast<uint16_t>(analogRead(player_state.m_sens_pin));
    uint16_t sensor_val = static_cast<uint16_t>(analogRead(player_state.m_coin_pin));
    uint16_t threshold = static_cast<uint16_t>(map(pot_val, 0, 1023, k_thresh_lo, k_thresh_hi));

    s_debug.print(millis());
    s_debug.print(F(" ms P"));
    s_debug.print(player_state.m_id);
    s_debug.print(F(" AIN: coin_adc="));
    s_debug.print(sensor_val);
    s_debug.print(F(" sensitivity_pot_adc="));
    s_debug.print(pot_val);
    s_debug.print(F(" detection_threshold_adc="));
    s_debug.print(threshold);
    s_debug.print(F(" below_threshold_adc="));
    s_debug.print(static_cast<int16_t>(threshold) - static_cast<int16_t>(sensor_val));
    s_debug.print(F(" release_threshold_adc="));
    s_debug.print(threshold + k_coin_hysteresis);
    s_debug.print(F(" detected="));
    s_debug.println(is_coin_present(sensor_val, threshold, player_state.m_coin_active) ? F("yes") : F("no"));
}


static void update_debug()
{
    static uint32_t last_readout_ms = 0U;
    uint32_t now = millis();

    if(DEBUG)
    {
        if((now - last_readout_ms) >= k_debug_interval_ms && Serial.dtr())
        {
            last_readout_ms = now;
            if(P1_ENABLE)
            {
                debug_analog(s_p1_state);
            }
            if(P2_ENABLE)
            {
                debug_analog(s_p2_state);
            }
        }
        s_debug.drain();
    }
}


/**
 * @brief Read a player's sensitivity potentiometer and return a scaled coin-detection threshold.
 *
 * Detection triggers below the threshold, so a higher threshold is more sensitive.
 *
 * @param player_state Reference to the player whose potentiometer is sampled.
 * @return uint16_t Threshold in ADC units, linearly mapped to [k_thresh_lo, k_thresh_hi].
 */
static uint16_t read_threshold(const PlayerState& player_state)
{
    uint16_t pot_val = static_cast<uint16_t>(analogRead(player_state.m_sens_pin));
    return static_cast<uint16_t>(map(pot_val, 0, 1023, k_thresh_lo, k_thresh_hi));
}


/**
 * @brief Decide whether a coin is in front of the sensor for a given reading.
 *
 * The ITR20001 phototransistor is wired with a pull-up on its collector and the signal
 * taken from that collector, so the input idles HIGH in darkness and is pulled DOWN as
 * reflected IR light increases. A coin is detected when the reading drops below the
 * threshold and released only once it rises to threshold + k_coin_hysteresis or higher,
 * so noise around the threshold cannot toggle the state.
 *
 * @param sensor_val  Raw coin sensor reading (ADC units).
 * @param threshold   Current detection threshold (ADC units).
 * @param coin_active Current detection state.
 * @return bool True when a coin is detected.
 */
static bool is_coin_present(uint16_t sensor_val, uint16_t threshold, bool coin_active)
{
    if(coin_active)
    {
        return sensor_val < (threshold + k_coin_hysteresis);
    }
    return sensor_val < threshold;
}


/**
 * @brief Send the credit key and start repeating PWM fading until Start is pressed.
 * @param player_state Reference to the player state to modify.
 */
static void trigger_credit(PlayerState& player_state)
{
    Keyboard.print(player_state.m_key_credit_ptr);
    player_state.m_dim_start_ms = millis();
    player_state.m_is_dimming   = true;
    if(DEBUG)
    {
        debug_event(player_state, F("Credit accepted; LED animation started"));
        s_debug.print(F("Credit key sent: "));
        s_debug.println(player_state.m_key_credit_ptr);
    }
}


/**
 * @brief Initialise a PlayerState struct and configure all associated hardware pins.
 *
 * @param player_state   Output: the PlayerState struct to initialise.
 * @param id             Player number used in debug output.
 * @param led_pin        PWM output pin for the start button LED.
 * @param but_pin        Digital input pin for the start button (active LOW, interrupt capable).
 * @param coin_but_pin   Digital input pin for the coin-insert button (active LOW, interrupt capable).
 * @param coin_pin       Analog input pin for the IR coin sensor.
 * @param sens_pin       Analog input pin for this player's sensitivity potentiometer.
 * @param key_start_ptr  Keyboard string to send when the start button is pressed.
 * @param key_credit_ptr Keyboard string to send when a coin is detected.
 */
static void init_player(PlayerState&  player_state,
                         uint8_t       id,
                         uint8_t       led_pin,
                         uint8_t       but_pin,
                         uint8_t       coin_but_pin,
                         uint8_t       coin_pin,
                         uint8_t       sens_pin,
                         const char*   key_start_ptr,
                         const char*   key_credit_ptr)
{
    player_state.m_id             = id;
    player_state.m_led_pin        = led_pin;
    player_state.m_coin_pin       = coin_pin;
    player_state.m_sens_pin       = sens_pin;
    player_state.m_key_start_ptr  = key_start_ptr;
    player_state.m_key_credit_ptr = key_credit_ptr;

    player_state.m_last_coin_ms       = millis() - static_cast<uint32_t>(COIN_DEBOUNCE_DELAY);
    player_state.m_dim_start_ms       = 0U;
    player_state.m_last_led_update_ms = 0U;
    player_state.m_coin_active        = false;
    player_state.m_is_dimming         = false;

    pinMode(led_pin,      OUTPUT);
    pinMode(sens_pin,     INPUT);

    analogWrite(led_pin, k_led_off);

    void (*start_isr)() = (1U == id) ? isr_p1_start : isr_p2_start;
    void (*coin_isr)()  = (1U == id) ? isr_p1_coin  : isr_p2_coin;
    init_button(player_state.m_start_but, but_pin,      start_isr);
    init_button(player_state.m_coin_but,  coin_but_pin, coin_isr);
}


/**
 * @brief Poll the IR coin sensor and fire a credit event on each new detection.
 *
 * The sensor input idles HIGH and is pulled down by reflected IR, so a coin is seen as a
 * falling ADC edge past the threshold. The reading must rise k_coin_hysteresis above the
 * threshold before another coin can be detected. A coin is accepted once per insertion;
 * COIN_DEBOUNCE_DELAY blocks new ADC credits for two seconds after an accepted coin.
 * Ignored detections do not extend the cooldown and must release before retriggering.
 *
 * @param player_state Reference to the player state.
 */
static void update_coin(PlayerState& player_state)
{
    uint16_t threshold  = read_threshold(player_state);
    uint16_t sensor_val = static_cast<uint16_t>(analogRead(player_state.m_coin_pin));
    uint32_t now        = millis();
    bool     coin_now   = is_coin_present(sensor_val, threshold, player_state.m_coin_active);

    if(true == coin_now && false == player_state.m_coin_active)
    {
        if((now - player_state.m_last_coin_ms) >= static_cast<uint32_t>(COIN_DEBOUNCE_DELAY))
        {
            if(DEBUG)
            {
                s_debug.print(now);
                s_debug.print(F(" ms P"));
                s_debug.print(player_state.m_id);
                s_debug.print(F(": IR coin detected; coin_adc="));
                s_debug.println(sensor_val);
            }
            trigger_credit(player_state);
            player_state.m_last_coin_ms = now;
        }
        else
        {
            debug_event(player_state, F("IR detection ignored (debounce)"));
        }
    }

    player_state.m_coin_active = coin_now;
}


/**
 * @brief Handle presses latched by the button ISRs and re-arm released buttons.
 *
 * Start: sends one start key per press, stops LED fading and turns the LED off.
 * Coin button: triggers one credit per press.
 *
 * @param player_state Reference to the player state.
 */
static void update_buttons(PlayerState& player_state)
{
    if(true == take_press(player_state.m_start_but))
    {
        Keyboard.print(player_state.m_key_start_ptr);
        player_state.m_is_dimming = false;
        analogWrite(player_state.m_led_pin, k_led_off);
        if(DEBUG)
        {
            debug_event(player_state, F("Start button pressed; LED fading stopped, LED off"));
            s_debug.print(F("Start key sent: "));
            s_debug.println(player_state.m_key_start_ptr);
        }
    }

    if(true == take_press(player_state.m_coin_but))
    {
        debug_event(player_state, F("Coin-insert button pressed"));
        trigger_credit(player_state);
    }

    rearm_button(player_state.m_start_but);
    rearm_button(player_state.m_coin_but);
}


/**
 * @brief Repeatedly fade the start button PWM LED after a credit, until Start is pressed.
 *
 * Phase 1 (first half of k_dim_duration_ms): fades up from k_led_min to k_led_full.
 * Phase 2 (second half): fades back down from k_led_full to k_led_min.
 * The cycle repeats while m_is_dimming is true.
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

    uint32_t elapsed  = (now - player_state.m_dim_start_ms) % static_cast<uint32_t>(k_dim_duration_ms);
    uint32_t half_dur = static_cast<uint32_t>(k_dim_duration_ms) / 2U;
    uint8_t  brightness;

    if(elapsed < half_dur)
    {
        brightness = static_cast<uint8_t>(map(static_cast<long>(elapsed), 0L,
                                              static_cast<long>(half_dur),
                                              k_led_min, k_led_full));
    }
    else
    {
        brightness = static_cast<uint8_t>(map(static_cast<long>(elapsed),
                                              static_cast<long>(half_dur),
                                              static_cast<long>(k_dim_duration_ms),
                                              k_led_full, k_led_min));
    }
    analogWrite(player_state.m_led_pin, brightness);
}


// ============================================================
// Arduino entry points
// ============================================================

/**
 * @brief Initialise USB keyboard HID and configure hardware for enabled players.
 */
void setup()
{
    if(DEBUG)
    {
        Serial.begin(115200);
    }
    Keyboard.begin();
    analogReference(DEFAULT);

    if(P1_ENABLE)
    {
        init_player(s_p1_state, 1U, P1_LED, P1_BUT, P1_COIN_BUT, P1_COIN,
                    P1_SENS, P1_KEY_START, P1_KEY_CREDIT);
    }
    if(P2_ENABLE)
    {
        init_player(s_p2_state, 2U, P2_LED, P2_BUT, P2_COIN_BUT, P2_COIN,
                    P2_SENS, P2_KEY_START, P2_KEY_CREDIT);
    }
    if(DEBUG)
    {
        s_debug.println(F("IRCoinSlot ready; debug AIN interval: 2000 ms"));
        s_debug.print(F("Sensitivity pot sets absolute threshold (ADC), detect below: "));
        s_debug.print(k_thresh_lo);
        s_debug.print(F(" to "));
        s_debug.println(k_thresh_hi);
        s_debug.print(F("Player 1: "));
        s_debug.println(P1_ENABLE ? F("enabled") : F("disabled"));
        s_debug.print(F("Player 2: "));
        s_debug.println(P2_ENABLE ? F("enabled") : F("disabled"));
    }
}


/**
 * @brief Main loop: handle latched button presses first, then poll sensors and update outputs.
 */
void loop()
{
    if(P1_ENABLE)
    {
        update_buttons(s_p1_state);
        update_coin(s_p1_state);
        update_led(s_p1_state);
    }

    if(P2_ENABLE)
    {
        update_buttons(s_p2_state);
        update_coin(s_p2_state);
        update_led(s_p2_state);
    }
    update_debug();
}
