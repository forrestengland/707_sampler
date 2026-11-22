#include <jack/midiport.h>
#include <jack/jack.h>
#include <sndfile.h>

#include <atomic>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ui.h"

constexpr int NUM_INSTRUMENTS = 16;
constexpr int MAX_VOICES = 64;
constexpr int QUEUE_SIZE = 256;

constexpr int MIDI_BASE_NOTE = 30;
constexpr int MIDI_NOTE_COUNT = 16;

constexpr int MAX_PATTERN_STEPS = 64;
constexpr int NUM_BANKS = 4;
constexpr int STEPS_PER_BANK = 16;

struct Pattern {
  bool steps[NUM_INSTRUMENTS][MAX_PATTERN_STEPS] = {};
  int lastStep = 16;
};

struct Sample {
  std::vector<float> data;
  uint64_t frames = 0;
  int channels = 0;
  int sampleRate = 0;
};

struct InstrumentParams {
  float level = 1.0f;
  float attack = 0.0f;
  float decay = 0.5f;
  float tune = 0.0f;       // Semitones
  float cutoff = 20000.0f;
  float resonance = 0.0f;  // 0-1
};

struct AtomicInstrumentParams {
  std::atomic<float> level{1.0f};
  std::atomic<float> attack{0.0f};
  std::atomic<float> decay{0.5f};
  std::atomic<float> tune{0.0f};
  std::atomic<float> cutoff{20000.0f};
  std::atomic<float> resonance{0.0f};
};

struct Instrument {
  Sample sample;
};

enum class EventType : uint8_t {
  Trigger,
  Select,
  Level,
  Attack,
  Decay,
  Tune,
  Cutoff,
  Resonance,
  TogglePatternStep,
  PatternStep,
  LastStep,
  Tempo,
  StartSequencer,
  StopSequencer,
  AdjustLevel,
  AdjustTune,
  AdjustCutoff,
  AdjustDecay
};

struct Event {
  EventType type;
  int instrument;
  float value;
  int step = 0;
};

// Single-producer, single-consumer event queue.
class EventQueue {
public:
  bool push(const Event& event) {
    uint32_t head = writePos.load(
				  std::memory_order_relaxed);

    uint32_t next = (head + 1) % QUEUE_SIZE;

    if (next == readPos.load(
			     std::memory_order_acquire)) {
      return false;
    }

    events[head] = event;

    writePos.store(
		   next, std::memory_order_release);

    return true;
  }

  bool pop(Event& event) {
    uint32_t tail = readPos.load(
				 std::memory_order_relaxed);

    if (tail == writePos.load(
			      std::memory_order_acquire)) {
      return false;
    }

    event = events[tail];

    readPos.store(
		  (tail + 1) % QUEUE_SIZE,
		  std::memory_order_release);

    return true;
  }

private:
  std::array<Event, QUEUE_SIZE> events{};

  std::atomic<uint32_t> writePos{0};
  std::atomic<uint32_t> readPos{0};
};

struct Voice {

  const Sample* sample = nullptr;

  int instrument = 0;

  double position = 0.0;
  double increment = 1.0;

  float volume = 1.0f;
  float elapsed = 0.0f;

  // State-variable filter memory
  float ic1 = 0.0f;
  float ic2 = 0.0f;

  bool active = false;
};

class AudioEngine {
public:
  bool getDisplayState(
		       int& instrument,
		       InstrumentParams& values
		       ) const {
    instrument = displaySelectedInstrument.load(
						std::memory_order_acquire
						);

    const auto& p = displayParams[instrument];

    values.level = p.level.load(std::memory_order_relaxed);
    values.attack = p.attack.load(std::memory_order_relaxed);
    values.decay = p.decay.load(std::memory_order_relaxed);
    values.tune = p.tune.load(std::memory_order_relaxed);
    values.cutoff = p.cutoff.load(std::memory_order_relaxed);
    values.resonance = p.resonance.load(std::memory_order_relaxed);

    return true;
  }
  
  bool loadSample(int instrument, const std::string& path) {
    if (instrument < 0 ||
	instrument >= NUM_INSTRUMENTS) {
      return false;
    }

    SF_INFO info{};
    SNDFILE* file = sf_open(
			    path.c_str(), SFM_READ, &info);

    if (!file) {
      std::cerr << "Cannot open sample: "
		<< path << "\n";
      return false;
    }

    if (info.channels != 1 && info.channels != 2) {
      std::cerr << "Only mono/stereo supported\n";
      sf_close(file);
      return false;
    }

    Sample temp;

    temp.channels = info.channels;
    temp.sampleRate = info.samplerate;
    temp.frames = info.frames;

    temp.data.resize(
		     static_cast<size_t>(info.frames) *
		     info.channels);

    sf_count_t framesRead = sf_readf_float(
					   file,
					   temp.data.data(),
					   info.frames);

    sf_close(file);

    if (framesRead <= 0) {
      std::cerr << "Empty or unreadable sample\n";
      return false;
    }

    temp.frames = static_cast<uint64_t>(framesRead);
    temp.data.resize(
		     static_cast<size_t>(framesRead) *
		     temp.channels);

    instruments[instrument].sample = std::move(temp);

    std::cout << "Loaded instrument "
	      << instrument
	      << ": " << path
	      << " (" << framesRead << " frames)\n";

    return true;
  }

  bool start() {
    jack_status_t status{};

    client = jack_client_open(
			      "DrumMachine",
			      JackNullOption,
			      &status);

    if (!client) {
      std::cerr << "Cannot connect to JACK\n";
      return false;
    }

    sampleRate = jack_get_sample_rate(client);

    leftPort = jack_port_register(
				  client,
				  "out_l",
				  JACK_DEFAULT_AUDIO_TYPE,
				  JackPortIsOutput,
				  0);

    rightPort = jack_port_register(
				   client,
				   "out_r",
				   JACK_DEFAULT_AUDIO_TYPE,
				   JackPortIsOutput,
				   0);

    if (!leftPort || !rightPort) {
      std::cerr << "Cannot create JACK ports\n";
      return false;
    }

    midiInputPort = jack_port_register(
				       client,
				       "midi_in",
				       JACK_DEFAULT_MIDI_TYPE,
				       JackPortIsInput,
				       0
				       );

    if (!midiInputPort) {
      std::cerr << "Cannot create MIDI input port\n";
      return false;
    }	

    jack_set_process_callback(
			      client, processCallback, this);

    if (jack_activate(client) != 0) {
      std::cerr << "Cannot activate JACK client\n";
      return false;
    }

    std::cout << "JACK running at "
	      << sampleRate << " Hz\n";

    return true;
  }

  void stop() {
    if (client) {
      jack_deactivate(client);
      jack_client_close(client);
      client = nullptr;
    }
  }

  bool trigger(int instrument) {
    return queue.push({
	EventType::Trigger,
	instrument,
	0.0f
      });
  }

  bool selectInstrument(int instrument) {
    return queue.push({
        EventType::Select, instrument, 0.0f
      });
  }

  bool setParameter(EventType type, int instrument, float value) {
    return queue.push({type, instrument, value});
  }

  bool setPatternStep(int instrument, int step, bool enabled) {
    if (instrument < 0 || instrument >= NUM_INSTRUMENTS ||
        step < 0 || step >= MAX_PATTERN_STEPS) {
      return false;
    }

    return queue.push({
        EventType::PatternStep,
        instrument,
        enabled ? 1.0f : 0.0f,
        step
      });
  }

  bool togglePatternStep(int instrument, int step) {
    if (instrument < 0 || instrument >= NUM_INSTRUMENTS ||
        step < 0 || step >= MAX_PATTERN_STEPS) {
      return false;
    }

    return queue.push({
        EventType::TogglePatternStep,
        instrument,
        0.0f,
        step
      });
  }  

  bool setLastStep(int step) {
    if (step < 1 || step > MAX_PATTERN_STEPS)
      return false;

    return queue.push({
        EventType::LastStep, 0, static_cast<float>(step)
      });
  }

  bool setTempo(float bpm) {
    if (bpm < 30.0f || bpm > 300.0f)
      return false;

    return queue.push({
        EventType::Tempo, 0, bpm
      });
  }

  bool startSequencer() {
    return queue.push({
        EventType::StartSequencer, 0, 0.0f
      });
  }

  bool stopSequencer() {
    return queue.push({
        EventType::StopSequencer, 0, 0.0f
      });
  }

  bool getPatternDisplayState(
			      int& instrument,
			      uint64_t& steps,
			      int& playingStep,
			      int& lastStep,
			      bool& running
			      ) const {
    instrument = displaySelectedInstrument.load(std::memory_order_acquire);
    steps = displayPatternSteps.load(std::memory_order_relaxed);
    playingStep = displayPlayingStep.load(std::memory_order_relaxed);
    lastStep = displayLastStep.load(std::memory_order_relaxed);
    running = displaySequencerRunning.load(std::memory_order_relaxed);

    return true;
  }  

private:
  jack_client_t* client = nullptr;

  jack_port_t* leftPort = nullptr;
  jack_port_t* rightPort = nullptr;

  jack_port_t* midiInputPort = nullptr;  

  uint32_t sampleRate = 44100;

  std::array<InstrumentParams, NUM_INSTRUMENTS> params;
  int selectedInstrument = 0;

  std::array<Instrument, NUM_INSTRUMENTS> instruments;
  std::array<Voice, MAX_VOICES> voices;

  std::array<float, NUM_INSTRUMENTS> volumes = [] {
    std::array<float, NUM_INSTRUMENTS> a{};
    a.fill(1.0f);
    return a;
  }();

  std::array<float, NUM_INSTRUMENTS> pitches = [] {
    std::array<float, NUM_INSTRUMENTS> a{};
    a.fill(1.0f);
    return a;
  }();

  Pattern pattern;

  float bpm = 120.0f;

  bool sequencerRunning = false;
  int currentStep = 0;

  // Countdown in audio frames until the next step.
  double framesUntilStep = 0.0;  

  EventQueue queue;

  std::array<AtomicInstrumentParams, NUM_INSTRUMENTS> displayParams{};

  std::atomic<int> displaySelectedInstrument{0};

  std::atomic<uint64_t> displayPatternSteps{0};
  std::atomic<int> displayPlayingStep{-1};
  std::atomic<bool> displaySequencerRunning{false};

  int playingStep = -1;

  std::atomic<int> displayLastStep{16};  

  void publishDisplayState() {

    for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
      const auto& source = params[i];
      auto& destination = displayParams[i];

      destination.level.store(source.level, std::memory_order_relaxed);
      destination.attack.store(source.attack, std::memory_order_relaxed);
      destination.decay.store(source.decay, std::memory_order_relaxed);
      destination.tune.store(source.tune, std::memory_order_relaxed);
      destination.cutoff.store(source.cutoff, std::memory_order_relaxed);
      destination.resonance.store(source.resonance, std::memory_order_relaxed);
    }

    uint64_t stepMask = 0;

    for (int s = 0; s < MAX_PATTERN_STEPS; ++s) {
      if (pattern.steps[selectedInstrument][s]) {
        stepMask |= (uint64_t{1} << s);
      }
    }

    displayPatternSteps.store(
			      stepMask,
			      std::memory_order_relaxed
			      );

    displayPlayingStep.store(
			     sequencerRunning ? playingStep : -1,
			     std::memory_order_relaxed
			     );

    displaySequencerRunning.store(
				  sequencerRunning,
				  std::memory_order_relaxed
				  );

    displaySelectedInstrument.store(
				    selectedInstrument,
				    std::memory_order_release
				    );

    displayLastStep.store(pattern.lastStep, std::memory_order_relaxed);    
  }  

  static int processCallback(
			     jack_nframes_t nframes,
			     void* arg) {

    return static_cast<AudioEngine*>(arg)
      ->process(nframes);
  }

  void handleEvents() {
    Event event;

    while (queue.pop(event)) {

      switch (event.type) {

      case EventType::Select:
	if (event.instrument >= 0 &&
	    event.instrument < NUM_INSTRUMENTS) {
	  selectedInstrument = event.instrument;
	}
	continue;

      case EventType::StartSequencer:
	playingStep = -1;
	currentStep = 0;
	framesUntilStep = 0.0;
	sequencerRunning = true;
	continue;

      case EventType::StopSequencer:
	sequencerRunning = false;
	currentStep = 0;
	playingStep = -1;		
	continue;

      case EventType::LastStep:
	pattern.lastStep = static_cast<int>(event.value);

	if (currentStep >= pattern.lastStep) {
	  currentStep = 0;
	}
	continue;

      case EventType::Tempo:
	bpm = std::clamp(event.value, 30.0f, 300.0f);
	continue;

      default:
	break;
      }

      int i = event.instrument;

      if (i < 0 || i >= NUM_INSTRUMENTS)
	continue;

      auto& p = params[i];

      switch (event.type) {

      case EventType::Trigger:
	startVoice(i);
	break;

      case EventType::PatternStep:
	if (event.step >= 0 &&
	    event.step < MAX_PATTERN_STEPS) {
	  pattern.steps[i][event.step] =
	    event.value != 0.0f;
	}
	break;
      case EventType::TogglePatternStep:
	if (event.step >= 0 &&
	    event.step < MAX_PATTERN_STEPS) {
	  pattern.steps[i][event.step] =
            !pattern.steps[i][event.step];
	}
	break;
      case EventType::Level:
	p.level = std::clamp(event.value, 0.0f, 1.0f);
	break;

      case EventType::Attack:
	p.attack = std::clamp(event.value, 0.0f, 0.5f);
	break;

      case EventType::Decay:
	p.decay = std::clamp(event.value, 0.005f, 5.0f);
	break;

      case EventType::Tune:
	p.tune = std::clamp(event.value, -24.0f, 24.0f);
	break;

      case EventType::Cutoff:
	p.cutoff = std::clamp(event.value, 100.0f, 20000.0f);
	break;

      case EventType::Resonance:
	p.resonance = std::clamp(event.value, 0.0f, 1.0f);
	break;

      case EventType::AdjustLevel:
	p.level = std::clamp(
			     p.level + event.value,
			     0.0f, 1.0f
			     );
	break;

      case EventType::AdjustTune:
	p.tune = std::clamp(
			    p.tune + event.value * 48.0f,
			    -24.0f, 24.0f
			    );
	break;

      case EventType::AdjustCutoff:
	p.cutoff = std::clamp(
			      p.cutoff * std::pow(200.0f, event.value),
			      100.0f, 20000.0f
			      );
	break;

      case EventType::AdjustDecay:
	p.decay = std::clamp(
			     p.decay * std::pow(1000.0f, event.value),
			     0.005f, 5.0f
			     );
	break;	

      default:
	break;
      }
    }
  }  

  void startVoice(int instrument, float velocity = 1.0f) {
    const Sample& sample = instruments[instrument].sample;

    if (sample.frames == 0)
      return;

    Voice* selected = nullptr;

    for (auto& voice : voices) {
      if (!voice.active) {
	selected = &voice;
	break;
      }
    }

    if (!selected)
      selected = &voices[0];

    const auto& p = params[instrument];

    selected->sample = &sample;
    selected->instrument = instrument;
    selected->position = 0.0;
    selected->elapsed = 0.0f;
    selected->increment =
      (static_cast<double>(sample.sampleRate) / sampleRate) *
      std::pow(2.0, p.tune / 12.0);

    selected->volume = p.level * velocity;

    selected->ic1 = 0.0f;
    selected->ic2 = 0.0f;
    selected->active = true;
  }

  void handleMidiCC(uint8_t cc, uint8_t value) {
    float v = value / 127.0f;
    int i = selectedInstrument;

    switch (cc) {
    case 7: // Level
      params[i].level = v;
      break;

    case 12: // Decay, logarithmic
      params[i].decay =
	0.005f * std::pow(1000.0f, v);
      break;

    case 13: // Tune
      params[i].tune = -24.0f + v * 48.0f;
      break;

    case 14: // Cutoff, logarithmic
      params[i].cutoff =
	100.0f * std::pow(200.0f, v);
      break;

    case 15: // Resonance
      params[i].resonance = v;
      break;

    case 1: // Modulation wheel: Attack
      params[i].attack = v * 0.5f;
      break;
    }
  }  

  int process(jack_nframes_t nframes) {

    float* outL = static_cast<float*>(jack_port_get_buffer(leftPort, nframes));

    float* outR = static_cast<float*>(jack_port_get_buffer(rightPort, nframes));

    void* midiBuffer = jack_port_get_buffer(midiInputPort, nframes);

    std::fill(outL, outL + nframes, 0.0f);
    std::fill(outR, outR + nframes, 0.0f);

    // Handle commands from the main thread.
    handleEvents();

    // Read MIDI events for this JACK block.
    uint32_t midiCount =
      jack_midi_get_event_count(midiBuffer);

    uint32_t midiIndex = 0;

    for (jack_nframes_t frame = 0;
	 frame < nframes;
	 ++frame) {

      // update sequencer
      // Sequencer clock: one step per sixteenth note.
      if (sequencerRunning && framesUntilStep <= 0.0) {

	playingStep = currentStep;

	for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
	  if (pattern.steps[i][currentStep]) {
            startVoice(i);
	  }
	}

	currentStep++;

	if (currentStep >= pattern.lastStep) {
	  currentStep = 0;
	}

	double framesPerStep =
	  static_cast<double>(sampleRate) * 60.0 /
	  (static_cast<double>(bpm) * 4.0);

	framesUntilStep += framesPerStep;
      }
      
      // Process MIDI events at their actual frame position.
      while (midiIndex < midiCount) {
	
	jack_midi_event_t event{};

	if (jack_midi_event_get(&event, midiBuffer, midiIndex) != 0) {
	  break;
	}

	if (event.time > frame) {
	  break;
	}

	if (event.size >= 3) {
	  const uint8_t* data = event.buffer;

	  uint8_t status = data[0] & 0xF0;
	  uint8_t note = data[1];
	  uint8_t velocity = data[2];

	  if (status == 0x90 && data[2] > 0) {
	    int instrument = data[1] - MIDI_BASE_NOTE;

	    if (instrument >= 0 && instrument < NUM_INSTRUMENTS) {
	      selectedInstrument = instrument;

	      float velocity = data[2] / 127.0f;
	      startVoice(instrument, velocity);
	    }
	  } else if (status == 0xB0) {
	    handleMidiCC(data[1], data[2]);
	  }	  
	}

	++midiIndex;
      }

      // Mix all active voices.
      for (auto& voice : voices) {

	if (!voice.active || !voice.sample) {
	  continue;
	}

	const Sample& sample = *voice.sample;

	uint64_t index =
	  static_cast<uint64_t>(voice.position);

	if (index >= sample.frames) {
	  voice.active = false;
	  continue;
	}

	uint64_t next =
	  std::min(index + 1, sample.frames - 1);

	float fraction =
	  static_cast<float>(
			     voice.position - index);

	auto interpolate = [&](int channel) {
	  size_t a =
	    index * sample.channels +
	    (sample.channels == 1 ? 0 : channel);

	  size_t b =
	    next * sample.channels +
	    (sample.channels == 1 ? 0 : channel);

	  return sample.data[a] +
	    (sample.data[b] - sample.data[a])
	    * fraction;
	};

	const auto& p = params[voice.instrument];

	float inputL = interpolate(0);
	float inputR = sample.channels == 2
	  ? interpolate(1)
	  : inputL;

	// Envelope
	float envelope = 1.0f;

	if (p.attack > 0.0001f &&
	    voice.elapsed < p.attack) {

	  envelope = voice.elapsed / p.attack;
	}

	float decayTime = std::max(0.005f, p.decay);

	float decayElapsed = std::max(0.0f, voice.elapsed - p.attack);

	envelope *= std::exp(-5.0f * decayElapsed / decayTime);

	// State-variable low-pass filter
	float cutoff = std::clamp(p.cutoff,
				  100.0f,
				  static_cast<float>(sampleRate) * 0.45f);

	float g = std::tan(3.14159265f * cutoff / sampleRate);

	float q = 0.707f + p.resonance * 11.3f;
	float k = 1.0f / q;

	float a1 = 1.0f / (1.0f + g * (g + k));
	float a2 = g * a1;
	float a3 = g * a2;

	auto filter = [&](float input) {

	  float v3 = input - voice.ic2;
	  float v1 = a1 * voice.ic1 + a2 * v3;
	  float v2 = voice.ic2 + a2 * voice.ic1 + a3 * v3;

	  voice.ic1 = 2.0f * v1 - voice.ic1;
	  voice.ic2 = 2.0f * v2 - voice.ic2;

	  return v2;
	};

	float filteredL = filter(inputL);
	float filteredR = sample.channels == 2
	  ? filter(inputR)
	  : filteredL;

	outL[frame] += filteredL * voice.volume * envelope;
	outR[frame] += filteredR * voice.volume * envelope;

	voice.position += voice.increment;
	voice.elapsed += 1.0f / sampleRate;
      }

      // update sequencer step update time
      if (sequencerRunning) {
	framesUntilStep -= 1.0;
      }	
      
    }

    // Safety limiter.
    for (jack_nframes_t i = 0; i < nframes; ++i) {
      outL[i] = std::clamp(outL[i], -1.0f, 1.0f);
      outR[i] = std::clamp(outR[i], -1.0f, 1.0f);
    }

    // safely update instrument parameters for display
    publishDisplayState();

    return 0;
  }
};

int main(int argc, char** argv) {

  AudioEngine engine;

  // Load samples before starting JACK.
  for (int i = 1; i + 1 < argc; i += 2) {
    int slot = std::atoi(argv[i]);

    if (!engine.loadSample(slot, argv[i + 1])) {
      return 1;
    }
  }

  if (!engine.start()) {
    return 1;
  }

  UI ui;

  if (!ui.init()) {
    engine.stop();
    return 1;
  }

  ui.setPatternStepCallback([&](int instrument, int step) {
    engine.togglePatternStep(instrument, step);
  });

  ui.setTransportCallback([&](bool start) {
    if (start) {
      engine.startSequencer();
    } else {
      engine.stopSequencer();
    }
  });

  ui.setInstrumentSelectCallback([&](int instrument) {
    engine.selectInstrument(instrument);
  });

  ui.setLastStepCallback([&](int lastStep) {
    engine.setLastStep(lastStep);
  });

  ui.setEncoderCallback(
			[&](int instrument, int encoder, float delta) {

			  EventType type;

			  switch (encoder) {
			  case 0:
			    type = EventType::AdjustLevel;
			    break;

			  case 1:
			    type = EventType::AdjustTune;
			    break;

			  case 2:
			    type = EventType::AdjustCutoff;
			    break;

			  case 3:
			    type = EventType::AdjustDecay;
			    break;

			  default:
			    return;
			  }

			  engine.setParameter(type, instrument, delta);
			}
			);  

  ui.run([&]() {
    int instrument;
    InstrumentParams params;

    if (engine.getDisplayState(instrument, params)) {
      InstrumentDisplayData data;
      data.instrument = instrument;
      data.level = params.level;
      data.attack = params.attack;
      data.decay = params.decay;
      data.tune = params.tune;
      data.cutoff = params.cutoff;
      data.resonance = params.resonance;
      ui.setInstrumentDisplayData(data);
    }

    uint64_t steps;
    int playingStep;
    int lastStep;
    bool running;

    if (engine.getPatternDisplayState(instrument, steps, playingStep,
				      lastStep, running)) {

      PatternDisplayData data;
      data.instrument = instrument;
      data.steps = steps;
      data.playingStep = playingStep;
      data.lastStep = lastStep;
      data.running = running;

      ui.setPatternDisplayData(data);
    }    
  });

  ui.shutdown();
  engine.stop();

  return 0;
}
