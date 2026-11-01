#pragma once
#include <Arduino.h>

enum SPIState {IDLE, SELECTING, SENDING, DESELECTING};

class ManualSPI {
    public:
      SPIState current_state;

      ManualSPI(int sck_pin, int mosi_pin, int cyc_time_ms_) {
        this->sck = sck_pin;
        this->mosi = mosi_pin;
        this->cyc_time_ms = cyc_time_ms_;
        this->current_state = IDLE;

        this->bit_counter = 0;
        this->byte_counter = 0;
      }

      void init() {
        pinMode(this->sck, OUTPUT);
        pinMode(this->mosi, OUTPUT);
        digitalWrite(this->sck, LOW);
        digitalWrite(this->mosi, LOW);
        
        this->last_tick_time_us = micros();
        this->mosi_state = false;
        this->sck_state = false;
        this->send_requested = false;
        this->start_of_tick = false;
      }

      bool ready_for_send() {
        return this->current_state == IDLE;
      }

      // queue a message for sending. Returns true 
      // if successfully queued, false otherwise.
      bool queue_message(uint8_t* message, size_t message_len, int cs_pin_) {
        if (this->current_state == IDLE) {
          this->message = message;
          this->message_len = message_len;
          this->send_requested = true;
          this->cs_pin = cs_pin_;
          return true;
        } 
        return true;
      }

      void run() {
        if (this->current_state == IDLE) {
          if (send_requested) {
            this->current_state = SELECTING;
            this->bit_counter = 0;
            this->byte_counter = 0;
            this->tick_counter = 0;
            this->last_tick_time_us = micros();
            this->sck_state = LOW;
            this->mosi_state = LOW;
            this->start_of_tick = true;
            this->send_requested = false;
            digitalWrite(this->cs_pin, LOW);
          }

        } else if (this->current_state == SELECTING) { 
          if ((micros() - this->last_tick_time_us) > (this->cyc_time_ms * 1000)) {
            this->current_state = SENDING;
            this->last_tick_time_us = micros();
          }
        } else if (this->current_state == SENDING) {
          if (this->tick_counter == (4 * this->message_len)) {
            this->current_state = DESELECTING;
            this->last_tick_time_us = micros();
            return;
          }
          // for each bit that we want to send, there will be 
          // 4 "ticks":
          // 1: set the MOSI pin (ticks 0, 4, 8...)
          // 2: flip SCK HIGH (ticks 1, 5, 9...)
          // 3: flip SCK low (ticks 2, 6, 10...)
          // 4: hold state to provide buffer for next bit

          long current_tick_time_us = micros();
          uint16_t delayus = this->cyc_time_ms * 1000;
          uint8_t mask = 0b10000000 >> this->bit_counter;
          
          if (this->tick_counter % 4 == 0) {
            this->sck_state = LOW;
            uint8_t mask = 0b10000000 >> this->bit_counter;

            if (mask & this->message[this->byte_counter]) {
              this->mosi_state = HIGH;
            } else {
              this->mosi_state = LOW;
            }

          } else if (((this->tick_counter + 3) % 4) == 0) {
            this->sck_state = HIGH;
  
          } else if (((this->tick_counter + 2) % 4) == 0) {
            this->sck_state = LOW;

          } 

          // start-of-tick behavior: we want to write the outputs
          if (this->start_of_tick) {
            digitalWrite(this->mosi, this->mosi_state);
            digitalWrite(this->sck, this->sck_state);
            this->start_of_tick = false;
          }

          // end-of-tick behavior: we want to increment 
          // tick, bit, and byte counters.
          if ((current_tick_time_us - this->last_tick_time_us) > (delayus / 4)) {
            if (((this->tick_counter + 1) % 4) == 0) {
              this->bit_counter++;
              if (this->bit_counter == 8) {
                this->byte_counter++;
                this->bit_counter = 0;
              }
            }

            this->tick_counter++;
            this->start_of_tick = true;
          }

        } else if (this->current_state == DESELECTING) {
          if ((micros() - this->last_tick_time_us) > (this->cyc_time_ms * 1000)) {
            digitalWrite(this->cs_pin, HIGH);
            this->current_state = IDLE;
          }
        }
      }

    private:
      int sck;
      int mosi;
      uint8_t cyc_time_ms;
      long last_tick_time_us;
      bool mosi_state;
      bool sck_state;

      int cs_pin;
      int bit_counter;
      int byte_counter;
      int tick_counter;
      bool start_of_tick;

      bool send_requested;
      uint8_t* message;
      size_t message_len;
};
