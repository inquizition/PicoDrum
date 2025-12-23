#include "midi.h"

//--------------------------------------------------------------------+
// MIDI Task
//--------------------------------------------------------------------+

// Variable that holds the current position in the sequence.
uint32_t note_pos = 0;
static uint8_t note = 40;

// Store example melody as an array of note values
uint8_t note_sequence[] =
{
  74,78,81,86,90,93,98,102,57,61,66,69,73,78,81,85,88,92,97,100,97,92,88,85,81,78,
  74,69,66,62,57,62,66,69,74,78,81,86,90,93,97,102,97,93,90,85,81,78,73,68,64,61,
  56,61,64,68,74,78,81,86,90,93,98,102
};

void change_note(uint8_t n)
{
  note = n;
  send_midi(50);
}

void send_midi(uint8_t velocity)
{
  uint8_t msg[3];
  // Previous positions in the note sequence.
  int previous = note_pos - 1;

  // If we currently are at position 0, set the
  // previous position to the last note in the sequence.
  if (previous < 0) previous = sizeof(note_sequence) - 1;

  // Send Note On for current position at full velocity (127) on channel 1.
  msg[0] = 0x90;                    // Note On - Channel 1
  msg[1] = note; // Note Number
  msg[2] = velocity;                     // Velocity
  tud_midi_n_stream_write(0, 0, msg, 3);

  // Send Note Off for previous note.
  msg[0] = 0x80;                    // Note Off - Channel 1
  msg[1] = note; // Note Number
  msg[2] = 0;                       // Velocity
  tud_midi_n_stream_write(0, 0, msg, 3);

  // Increment position
  note_pos++;

  // If we are at the end of the sequence, start over.
  if (note_pos >= sizeof(note_sequence)) note_pos = 0;
}
