#include "daisy_seed.h"
#include "daisysp.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <random>
#include <chrono>

using namespace daisy;
using namespace daisysp;
using namespace std;

/*
	- Steps: Number of steps in the sequence
	- Active_step: The current active step, is incremented for each played
	note
	- mode_int: An integer corresponding to the current mode, i.e:
	Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian or Locrian.
	Might not be needed in the current implementation.
	- selected_note: which note in the sequence is currently selected (0-7 range)
	- page_adder: if the second "page" is selected, which are the other 8 beats 
	ranging from 9 - 16, the page_adder is equal to 8. This will be added in the
	"handle_sequence_buttons" function, which will then refer to the correct note

	- LOW_RANGE/HIGH_RANGE: Constants for BPM range for the BPM input potentiometer.
	- CUTOFF_MAX/CUTOFF_MIN: Range for cutoff potentiometer
	- DECAY_MAX/DECAY_MIN: Range for the decay potentiometer.
	- MAX_RESONANCE: The maximum value for the resonance potentiometer.
	- FILTER_MOVEMENT: How much the filter will move with each note.
	Thought this will correspond to amount in frequency, but not sure

	- env_mod: Variable for env_mod potentiometer.
	- cutoff: Variable for cutoff potentiometer.
	- tempo_bpm: The current tempo of the sequencer.

	- mode: This string specifies the steps for the scale going from the 
	root note upwards. It starts from the major scale (Ionian), which for
	C is  "C", "D", "E", "F", "G", "A", "B". 
	This string will be left shifted to the left to change the mode.
	- active: True/False if the sequencer is active or not. 
	- current_note: Will change based on array "activated_notes" and determine
	if the current step should be played or not. Only updated each tick.

	- time_at_boot: Used as first seed for random generator.
*/

int const steps = 16;
int active_step = 0;
int mode_int = 0;
int selected_note = 0;
int page_adder = 0;

int const NUMBER_OF_POTS = 7;

int const LOW_RANGE_BPM = 30;
int const HIGH_RANGE_BPM = 330;

int const CUTOFF_MAX = 16000;
int const CUTOFF_MIN = 0;

float const DECAY_MAX = 0.9;
float const DECAY_MIN = 0.01;

float const MAX_RESONANCE = 0.89; // old: 0.89

int const FILTER_MOVEMENT = 11000;

float env_mod = 0.8;
float cutoff = 13000.f;
float tempo_bpm = 120.f;

string mode = "HWWHWWW"; // W = Whole step, H = Half step  
bool active = false;
bool current_note = true;

chrono::high_resolution_clock::time_point time_at_boot = chrono::high_resolution_clock::now();
random_device rd;

/*
	- Hardware starts audio and controls daisy seed functionality.
	- Osc is the oscillator for the bass sound of the sequncer.
	- Envelopes for volume and for pitch of the bass.
	- Switches:
		- activate_sequence: Starting/stopping sequence
		- random_sequnce: Randomly generated sequence based of of 
		current scale.
		- switch_mode: Changes the modal character of the sound. I.e 
		from Ionian to Dorian. Basically means to increase specific notes
		by a half step. (read more: https://www.classical-music.com/features/articles/modes-in-music-what-they-are-and-how-they-are-used-in-music/)
		- select_note: select which note in the sequence that should have
		their pitch changed.
		- change_pitch: increase the pitch by one step, based on scale, 
		goes back around when reaching maximum
	- AdcChannelConfig (pots): Array storing all the pot variables, 
	currently holding: 	tempo, cut-off, resonance, pitch, decay, env_mod, dist
	- seq_buttons: GPIO Buttons for each note in the sequence, used to control
	the pitch and glide for notes, aswell if they are active or not.
	
	- Tick for keeping time using the tick.process() function, returning
	true when a tick is "active". The tick is set to a specific interval
	and is activated as long as the AudioCallback (infinite loop) is active.
	
*/

DaisySeed hardware;
Oscillator osc;
//infrasonic::MoogLadder flt; 
MoogLadder flt;
Overdrive dist;
AdEnv synthVolEnv, synthPitchEnv;
//Switch activate_sequence, random_sequence, switch_mode, activate_slide, 
Switch change_page, activate_slide;
uint16_t activate_state = 0, random_state = 0, switch_state = 0, slide_state = 0;
GPIO activate_sequence, random_sequence, switch_mode;
AdcChannelConfig pots[NUMBER_OF_POTS];
GPIO seq_button1, seq_button2, seq_button3, seq_button4, seq_button5, seq_button6, seq_button7, seq_button8;
vector<GPIO> seq_buttons(8);

Metro tick;

//GPIO debug_led;
GPIO page_led;
GPIO led_decoder_out1, led_decoder_out2, led_decoder_out3;

/*
	- notes: map for storage of the notes, key - value pairs for
	notes and their corresponding frequencies from low to high
	- all_notes: specifiying each note avaialable, aswell as one 
	octave of the root note. Root note is basicaly only relevant
	when the mode button is used. It will currently be in relation 
	to C. Changing the root 
*/
unordered_map<string, vector<double>> notes = {
    {"C", {16.35, 32.70, 65.41, 130.81, 261.63, 523.25, 1046.50, 2093.00, 4186.01}},
    {"Db", {17.32, 34.65, 69.30, 138.59, 277.18, 554.37, 1108.73, 2217.46, 4434.92}},
    {"D", {18.35, 36.71, 73.42, 146.83, 293.66, 587.33, 1174.66, 2349.32, 4698.64}},
    {"Eb", {19.45, 38.89, 77.78, 155.56, 311.13, 622.25, 1244.51, 2489.02, 4978.03}},
    {"E", {20.60, 41.20, 82.41, 164.81, 329.63, 659.26, 1318.51, 2637.02}},
    {"F", {21.83, 43.65, 87.31, 174.61, 349.23, 698.46, 1396.91, 2793.83}},
    {"Gb", {23.12, 46.25, 92.50, 185.00, 369.99, 739.99, 1479.98, 2959.96}},
    {"G", {24.50, 49.00, 98.00, 196.00, 392.00, 783.99, 1567.98, 3135.96}},
    {"Ab", {25.96, 51.91, 103.83, 207.65, 415.30, 830.61, 1661.22, 3322.44}},
    {"A", {27.50, 55.00, 110.00, 220.00, 440.00, 880.00, 1760.00, 3520.00}},
    {"Bb", {29.14, 58.27, 116.54, 233.08, 466.16, 932.33, 1864.66, 3729.31}},
    {"B", {30.87, 61.74, 123.47, 246.94, 493.88, 987.77, 1975.53, 3951.07}}
};

vector<string> all_notes = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B","C2"};
vector<string> scale = all_notes; // Chromatic
vector<string> sequence (steps, scale[0]);
vector<bool> slide(steps, false);
vector<bool> activated_notes(steps, true);

/**
 * @brief
 * 	For changing the pitch of the synth. (Could be done easier)
 */

void setPitch(double freq){
    synthPitchEnv.SetMax(freq);
    synthPitchEnv.SetMin(freq);
}

void setSlide(double note, double note_before){
	synthPitchEnv.SetMax(note);
	synthPitchEnv.SetMin(note_before);
	synthPitchEnv.SetTime(ADENV_SEG_DECAY, static_cast<float>(60/tempo_bpm));
}

/**
 * @brief 
 * Shifts the mode string to the left one step. "WWHWWWH" becomes "WHWWWHW"
 */

string circularShiftLeft(string mode) {
    char first = mode[0];
    mode.erase(0, 1);
    return mode += first;
}

/**
 * @brief 
 * Shifts the array of all notes if the root note is to be changed.
 * Then the modes will be taken from a "new pool" starting with a new 
 * root note.
 */
	
vector<string> circularShiftLeftArray(vector<string> array){
    vector<string> new_array(array);
    rotate(new_array.begin(), new_array.begin() + 1, new_array.end());
    return new_array;
}


/**
 * @brief 
 * 	Generates a new scale based on the current one. This function will
 * 	insert notes into the global "scale" variable based on the steps in
 * the "mode" string. If there is a "W" (whole-step) it will "jump" two
 * steps, "semi-tones", in the all_notes array, otherwise just one step.
 */


vector<string> generateScale(){
    vector<string> new_scale(8);

    int index = 0;
    size_t notes_collected = 0;
    while (notes_collected < 8)
    {
        new_scale[notes_collected] = all_notes[index % all_notes.size()];
        index += (mode[notes_collected] == 'W') ? 2 : 1;
        notes_collected++;
    }  
    return new_scale;      
}

/**
 * @brief 
 * Returns a new sequence with the same size as the old one which has
 * randomly generated notes taken from the "scale pool" of notes.
 * The seed is based on the boot time - the current time, combined
 * with the value of the random device "rd". Inefficient(?)
 */

mt19937 generateRandomEngine() {
	auto current_time = chrono::high_resolution_clock::now();
    unsigned seed = static_cast<unsigned>(chrono::high_resolution_clock::duration(time_at_boot - current_time).count() ^ rd());
    //unsigned seed = static_cast<unsigned>(programStart.time_since_epoch().count()*100);
	return mt19937(seed);
}

vector<string> randomizeSequence(){
	mt19937 rng = generateRandomEngine();
    vector<string> resulting_sequence(sequence.size());

    for(int i = 0; i < static_cast<int>(resulting_sequence.size()); i++){    
        uniform_int_distribution<unsigned> distrib(0, scale.size() - 1);
		int randomIndex = distrib(rng);
		resulting_sequence[i] = scale[randomIndex];
    }

    return resulting_sequence;
}

float convertBPMtoFreq(float bpm){
	return (bpm / 60.f)*8.f;
}

void increasePitchForActiveNote(){
	for(int i = 0; i < 8; i++){
		if(sequence[selected_note] == scale[i] && i == 7){
			sequence[selected_note] = scale[1];
			break;
		}
		else if (sequence[selected_note] == scale[i]){
			sequence[selected_note] = scale[i+1];
			break;
		}
	}
}

bool debounce_shift(GPIO &button, uint16_t &state) {
  //static uint16_t state = 0;
  state = (state << 1) | button.Read() | 0xfe00;
  return (state == 0xff00);
}

/*
bool debounce_shift(GPIO button, uint16_t &state) {
  //static uint16_t state = 0;
  state = (state << 1) | button.Read() | 0xfe00;
  return (state == 0xff00);
}*/


/*
	Global variables for checking the last states of the sequencer
	buttons and the counters for each.
*/

//vector<bool> last_button_states(8, false);
//vector<uint16_t> last_button_states(8, 0);
uint16_t last_button_states[8] = {0};
vector<int> counters(8, 0);

/**
 * @brief When the button is active for a certain amount of cycles 
 * (stable_threshold) the press is considered valid and used.
 * 
 */

bool debounce(GPIO button, bool last_button_state, int counter){
	const int stable_threshold = 360000; // 100 000

	bool button_state = /*!*/button.Read();
	
	// Check if the button state has changed
	if (button_state != last_button_state)
	{
		// Wait for a short period to filter out noise
		// std::this_thread::sleep_for(std::chrono::milliseconds(20));

		// Read the button state again
		button_state = /*!*/button.Read();

		// Check if the new button state is stable
		if (button_state == last_button_state)
		{
			counter++;

			// Check if the button state has been stable for the threshold
			if (counter >= stable_threshold) {
				return button_state;  // Debounced button state
			}
		}
		else
		{
			counter = 0;  // Reset the counter if the state changes
		}
	}

	last_button_state = button_state;
	return last_button_state;
}


chrono::milliseconds lastDebounceTimes[8] = {chrono::milliseconds(0)};

// Function to perform software debounce for a button press
bool debounceButton(GPIO button, chrono::milliseconds &lastDebounceTime) {
  static const chrono::milliseconds debounceDelay(50); // Set the debounce interval

  auto currentMillis = chrono::duration_cast<chrono::milliseconds>(
    chrono::system_clock::now().time_since_epoch()
  );

  if (currentMillis - lastDebounceTime >= debounceDelay) {
    lastDebounceTime = currentMillis;
    if (button.Read()) {
      return true; // Button press detected and debounced
    }
  }

  return false; // Button press not detected yet
}

/**
 * @brief 
 * Activating slide is straight-forward...
 * If the pitch is set to 0, the selected note (seq_buttons[i]) is
 * activated/deactivated 
 * Press slide button before pressing the note in the sequence.
 */

void handleSequenceButtons(){
	for(int i = 0; i < 8; i++){ // 8 = number of buttons
		//if(debounce(seq_buttons[i], last_button_states[i], counters[i])){
		//if(debounce(seq_buttons[i], last_button_states[i], counters[i])) {
		if(debounce_shift(seq_buttons[i], last_button_states[i])) {
			if(!activate_slide.Pressed())
				slide[i + page_adder] = !slide[i + page_adder];
			else{
				int pitch = hardware.adc.GetFloat(3) * scale.size(); // 0 - 7
				if(pitch == 0)
					activated_notes[i + page_adder] = !activated_notes[i + page_adder];
				else{
					sequence[i + page_adder] = scale[pitch];
					activated_notes[i + page_adder] = true;
				}
			}
		}
	}
}

//bool last_page_button_state = false;
//int page_button_counter = 0;

void inputHandler(){
	// Filters out noise from button-press.	
	
	activate_slide.Debounce();
	change_page.Debounce();
	
	if(debounce_shift(activate_sequence, activate_state))
        active = !active;

	if(change_page.RisingEdge()){
		page_adder = (page_adder + 8) % 16; // cycles between 8 or 0
		page_led.Write(page_adder);
	}

	if(debounce_shift(random_sequence, random_state)){
        active_step = 0;
        sequence = randomizeSequence();
    }
	
	if(debounce_shift(switch_mode, switch_state)){
        if(mode_int == 7) mode_int = 0;
        else mode_int++;

		if(mode_int != 0){ // not chromatic
        	mode = circularShiftLeft(mode);
			scale = generateScale();
		}
		else scale = all_notes;

        // Temporarily fill sequence with notes from scale.
		for(size_t i = 0; i < steps; i++)
			sequence[i] = scale[i % scale.size()];

		
    }
	//if(debounce(change_page, last_page_button_state, page_button_counter))
	//	page_adder = (page_adder + 8) % 16; // cycles between 8 or 0

	
	//debug_led.Write(seq_button1.Read());


	handleSequenceButtons();

	tempo_bpm = floor((hardware.adc.GetFloat(0) * (HIGH_RANGE_BPM - LOW_RANGE_BPM)) + LOW_RANGE_BPM); // BPM range from 30-300
	tick.SetFreq(convertBPMtoFreq(tempo_bpm));
	
	cutoff = hardware.adc.GetFloat(1) * (CUTOFF_MAX - CUTOFF_MIN) + CUTOFF_MIN;

	float resonance = hardware.adc.GetFloat(2) * (MAX_RESONANCE); // 0 - 0.89
	flt.SetRes(resonance);

	float decay = hardware.adc.GetFloat(4) * (DECAY_MAX - DECAY_MIN) + DECAY_MIN;
	synthVolEnv.SetTime(ADENV_SEG_DECAY, decay);
	
	env_mod = hardware.adc.GetFloat(5) * 1.0;
	dist.SetDrive(hardware.adc.GetFloat(6) * 0.7);
}	

/**
 * @brief 
 * Prepares the sample for the output audio. 
 * Signal processing is difficult...
 */
	

void prepareAudioBlock(size_t size, AudioHandle::InterleavingOutputBuffer out){
	float osc_out, synth_env_out, sig;
	for(size_t i = 0; i < size; i += 2) {
		//Get the next volume samples
		synth_env_out = synthVolEnv.Process();
		//Apply the pitch envelope to the synth
		osc.SetFreq(synthPitchEnv.Process());
		//Set the synth volume to the envelope's output
		osc.SetAmp(synth_env_out);
		//Process the next oscillator sample
		osc_out = osc.Process();
		
		// Blend cutoff with movement based on envelope
		flt.SetFreq(env_mod * synth_env_out * FILTER_MOVEMENT + cutoff);
		
		sig = dist.Process(flt.Process(osc_out));

		out[i]     = sig;
		out[i + 1] = sig;
	}
}

double getFreqOfNote(string note){
	double current_freq;
	if(note == "C2")
		current_freq = notes[note.substr(0,1)][3];
	else
		current_freq = notes[note][2];

	return current_freq;
}

/**
 * @brief 
 * Handles negative numbers, true modulo
 * @param dividend 
 * @param divisor 
 * @return int 
 */

int modulo(int dividend, int divisor){
	return (dividend % divisor + divisor) % divisor;
}
	
/**
 * @brief 
 * Triggers a note in the sequence, and increases the active step.
 * If the active step is at the last place, and the synth is at the first 
 * mode it wants to access the C note one octave above (one place forward
 * in the map with frequencies for each note).
 */
	

void triggerSequence(){
	if(tick.Process()){
		// Access the current note in the scale
		// int adder = page_adder & 8 ? 0x007 : 0x000;
		// int mask = page_adder ? 15 : 7;

		bool current_page = !((active_step >> 3) ^ (page_adder >> 3));
		led_decoder_out1.Write(current_page * (active_step & 0x1));
		led_decoder_out2.Write(current_page * (active_step & 0x2));
		led_decoder_out3.Write(current_page * (active_step & 0x4));

		string note = sequence[active_step];
		double current_freq = getFreqOfNote(note);
		
		if(slide[active_step]){
			double previous_freq = getFreqOfNote(sequence[modulo((active_step - 1), steps)]); 
			setSlide(current_freq, previous_freq);
		}
		else
			setPitch(current_freq);
		synthVolEnv.Trigger();
		synthPitchEnv.Trigger();
		
		// Increase the step in sequence, and set the next current note
		active_step = (active_step + 1) % steps;
		current_note = activated_notes[active_step];
	}
}

/**
 * @brief 
 * Configure and Initialize the Daisy Seed
 * These are separate to allow reconfiguration of any of the internal
 * components before initialization.
 * Block size refers to the number of samples handled per callback
*/	

void configureAndInitHardware(){
	hardware.Configure();
	hardware.Init();
	hardware.SetAudioBlockSize(4); 
	//hardware.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
}

/**
 * @brief 
 * Initialize oscillator for synthesizer, and set initial amplitude
 * to 1.
 */

void initOscillator(float samplerate){
    osc.Init(samplerate);
    osc.SetWaveform(Oscillator::WAVE_SAW);
    osc.SetAmp(1);
}

/**
 * @brief 
 * This envelope will control the kick oscillator's pitch
 * Note that this envelope is much faster than the volume
 */

void initPitchEnv(float samplerate){	
    synthPitchEnv.Init(samplerate);
    synthPitchEnv.SetTime(ADENV_SEG_ATTACK, .01);
    synthPitchEnv.SetTime(ADENV_SEG_DECAY, .05);
    synthPitchEnv.SetMax(400);
    synthPitchEnv.SetMin(400);
}

/** 
 * @brief 
 * This one will control the kick's volume
 */

void initVolEnv(float samplerate){
	synthVolEnv.Init(samplerate);
    synthVolEnv.SetTime(ADENV_SEG_ATTACK, .01);
    synthVolEnv.SetTime(ADENV_SEG_DECAY, 1);
    synthVolEnv.SetMax(1);
    synthVolEnv.SetMin(0);
}

 /**
  * @brief 
  * Initialize the buttons on pins 28, 27 and 25. (35, 34, 32 on the
  * daisy seed.)
  * The callback rate is samplerate / blocksize (48)
  */
	

void initButtons(float samplerate){
	//activate_sequence.Init(hardware.GetPin(28), samplerate / 48.f); // 35
    //random_sequence.Init(hardware.GetPin(27), samplerate / 48.f); // 34
    //switch_mode.Init(hardware.GetPin(25), samplerate / 48.f); // 32
	
	activate_sequence.Init(daisy::seed::D9, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
	random_sequence.Init(daisy::seed::D10, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
	switch_mode.Init(daisy::seed::D11, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
	//activate_slide.Init(daisy::seed::D15, GPIO::Mode::INPUT, GPIO::Pull::PULLUP);

	activate_slide.Init(hardware.GetPin(15), samplerate / 48.f); // 22
	change_page.Init(hardware.GetPin(24), samplerate / 48.f);
}

void initPots(){
	pots[0].InitSingle(hardware.GetPin(16)); // 23, change bpm
	pots[1].InitSingle(hardware.GetPin(17)); // 24, change cut-off freq
	pots[2].InitSingle(hardware.GetPin(18)); // 25, change resonance
	pots[3].InitSingle(hardware.GetPin(21)); // 28, note pitch
	pots[4].InitSingle(hardware.GetPin(20)); // 27, decay
	pots[5].InitSingle(hardware.GetPin(19)); // 26, env_mod
	pots[6].InitSingle(hardware.GetPin(28)); // 35, drive
	hardware.adc.Init(pots, NUMBER_OF_POTS); // Set ADC to use our configuration, and how many pots
}

void initFilter(float samplerate){
	flt.Init(samplerate);
	flt.SetRes(0.7);
	flt.SetFreq(700);
}

/**
 * @brief 
 * Initialize Metro object at bpm (ex 120) divided by 60 resulting 
 * in the freq for a note for each 4th beat. Multiply by 4 to get 
 * for each beat.
*/	

void initTick(float samplerate){
    tick.Init((tempo_bpm / 60.f)*4.f, samplerate);
}


void initSeqButtons(){
	seq_button1.Init(daisy::seed::D1, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
	seq_button2.Init(daisy::seed::D2, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
	seq_button3.Init(daisy::seed::D3, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
	seq_button4.Init(daisy::seed::D4, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
	seq_button5.Init(daisy::seed::D6, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
	seq_button6.Init(daisy::seed::D5, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
	seq_button7.Init(daisy::seed::D7, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);
	seq_button8.Init(daisy::seed::D8, GPIO::Mode::INPUT, GPIO::Pull::NOPULL);

	seq_buttons = {seq_button1, seq_button2, seq_button3, seq_button4, seq_button5, seq_button6, seq_button7, seq_button8};
}

void playSequence(size_t size, AudioHandle::InterleavingOutputBuffer out){
	if(active) {
		prepareAudioBlock(size, out);
		// Change decoder write here if want to see led light up on inactive steps aswell
		if(current_note)
			triggerSequence();

		else if (tick.Process()) {
			active_step = (active_step + 1) % steps;
			current_note = activated_notes[active_step];
		}
	}
	else
		for(size_t i = 0; i < size; i += 2) {
			out[i] = out[i] * 0.9; // Audio ramp-down
			out[i + 1] = out[i] * 0.9;
		}
}

void AudioCallback(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size) {
	inputHandler();
	playSequence(size, out);
}

int main(void) {
	configureAndInitHardware();
	
	float samplerate = hardware.AudioSampleRate();
	
	initOscillator(samplerate);
	initPitchEnv(samplerate);
	initVolEnv(samplerate);
	initButtons(samplerate);
	initPots();
	initFilter(samplerate);
	initTick(samplerate);
	initSeqButtons();
	dist.SetDrive(0.5);
	
	//change_page.Init(daisy::seed::D5, GPIO::Mode::INPUT);
	led_decoder_out1.Init(daisy::seed::D12, GPIO::Mode::OUTPUT);
	led_decoder_out2.Init(daisy::seed::D13, GPIO::Mode::OUTPUT);
	led_decoder_out3.Init(daisy::seed::D14, GPIO::Mode::OUTPUT);
	page_led.Init(daisy::seed::D23, GPIO::Mode::OUTPUT);
    
	//debug_led.Init(daisy::seed::D2, GPIO::Mode::OUTPUT);

	GPIO demux;
	demux.Init(daisy::seed::D22, GPIO::Mode::OUTPUT);
	demux.Write(false); // or false, depending on decoder

    /* 
		Initialize random generator, and start callback.
	*/
    
	hardware.adc.Start(); // Start ADC
    hardware.StartAudio(AudioCallback);
    // Loop forever
    for(;;) {}
}