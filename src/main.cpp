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
    Resonance
};

struct Event {
  EventType type;
  int instrument;
  float value;
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

  EventQueue queue;

  static int processCallback(
			     jack_nframes_t nframes,
			     void* arg) {

    return static_cast<AudioEngine*>(arg)
      ->process(nframes);
  }

  void handleEvents() {
    Event event;

    while (queue.pop(event)) {
      if (event.type == EventType::Select) {
	if (event.instrument >= 0 &&
	    event.instrument < NUM_INSTRUMENTS) {
	  selectedInstrument = event.instrument;
	}
	continue;
      }

      int i = event.instrument;

      if (i < 0 || i >= NUM_INSTRUMENTS)
	continue;

      auto& p = params[i];

      switch (event.type) {
      case EventType::Trigger:
	startVoice(i);
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
    }

    // Safety limiter.
    for (jack_nframes_t i = 0; i < nframes; ++i) {
      outL[i] = std::clamp(outL[i], -1.0f, 1.0f);
      outR[i] = std::clamp(outR[i], -1.0f, 1.0f);
    }

    return 0;
  }
};

int main(int argc, char** argv) {
  
  AudioEngine engine;

  Pattern pattern;
  int selectedBank = 0;  // 0-3  

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

  std::cout << "\nCommands:\n"
	    << " hit <instrument>\n"
	    << " select <instrument>\n"
	    << " level <instrument> <0.0-1.0>\n"
	    << " attack <instrument> <0.0-0.5>\n"
	    << " decay <instrument> <0.005-5.0>\n"
	    << " tune <instrument> <-24 to 24>\n"
	    << " cutoff <instrument> <100-20000>\n"
	    << " resonance <instrument> <0.0-1.0>\n"
	    << " bank <1-4>\n"
	    << " laststep <1-64>\n"
	    << " step <instrument> <step 1-16> <0|1>\n"
	    << " show\n"
	    << " quit\n\n";  

  std::string line;

  while (std::getline(std::cin, line)) {
    std::istringstream input(line);

    std::string command;
    int instrument;
    float value;

    input >> command;

    if (command == "quit") {
      break;
    }

    else if (command == "hit") {
      if (input >> instrument)
        engine.trigger(instrument);
    }

    else if (command == "select") {
      if (input >> instrument)
        engine.selectInstrument(instrument);
    } else if (command == "bank") {

      int bank;

      if (input >> bank) {
        if (bank < 1 || bank > NUM_BANKS) {
	  std::cout << "Bank must be 1-4.\n";
        } else {
	  selectedBank = bank - 1;

	  int firstStep = selectedBank * STEPS_PER_BANK + 1;
	  int lastBankStep = firstStep + STEPS_PER_BANK - 1;

	  std::cout << "Selected bank " << bank
		    << " (steps " << firstStep
		    << "-" << lastBankStep << ")\n";
        }
      }
    }

    else if (command == "laststep") {
      int length;

      if (input >> length) {
        if (length < 1 || length > MAX_PATTERN_STEPS) {
	  std::cout << "Pattern length must be 1-64.\n";
        } else {
	  pattern.lastStep = length;

	  std::cout << "Pattern length set to "
		    << pattern.lastStep << " steps.\n";
        }
      }
    }

    else if (command == "step") {
      int instrument;
      int localStep;
      int enabled;

      if (input >> instrument >> localStep >> enabled) {
        if (instrument < 0 || instrument >= NUM_INSTRUMENTS) {
	  std::cout << "Instrument must be 0-15.\n";
        }
        else if (localStep < 1 || localStep > STEPS_PER_BANK) {
	  std::cout << "Step must be 1-16 within the selected bank.\n";
        }
        else if (enabled != 0 && enabled != 1) {
	  std::cout << "Step value must be 0 or 1.\n";
        }
        else {
	  int absoluteStep =
	    selectedBank * STEPS_PER_BANK + localStep - 1;

	  pattern.steps[instrument][absoluteStep] = (enabled != 0);

	  std::cout << "Instrument " << instrument
		    << ", step " << absoluteStep + 1
		    << " set to " << enabled << ".\n";
        }
      }
    }

    else if (command == "show") {
      std::cout << "\nPattern length: "
		<< pattern.lastStep << " steps\n";

      std::cout << "Selected bank: "
		<< selectedBank + 1 << "\n\n";

      std::cout << "Instrument ";

      for (int s = 0; s < STEPS_PER_BANK; ++s) {
        std::cout << (s + 1) % 10;
      }

      std::cout << "\n";

      int firstStep = selectedBank * STEPS_PER_BANK;

      for (int i = 0; i < NUM_INSTRUMENTS; ++i) {
        std::cout << "     " << i << "      ";

        for (int s = 0; s < STEPS_PER_BANK; ++s) {
	  int absoluteStep = firstStep + s;

	  std::cout << (pattern.steps[i][absoluteStep] ? "X" : ".");
        }

        std::cout << "\n";
      }

      std::cout << "\n";
    }

    else {
      if (!(input >> instrument >> value))
        continue;

      EventType type;

      if (command == "level")
        type = EventType::Level;
      else if (command == "attack")
        type = EventType::Attack;
      else if (command == "decay")
        type = EventType::Decay;
      else if (command == "tune")
        type = EventType::Tune;
      else if (command == "cutoff")
        type = EventType::Cutoff;
      else if (command == "resonance")
        type = EventType::Resonance;
      else
        continue;

      engine.setParameter(type, instrument, value);
    }    

  }

  engine.stop();

  return 0;
}
