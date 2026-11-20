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

constexpr int MIDI_BASE_NOTE = 36;
constexpr int MIDI_NOTE_COUNT = 16;

struct Sample {
  std::vector<float> data;
  uint64_t frames = 0;
  int channels = 0;
  int sampleRate = 0;
};

struct Instrument {
  Sample sample;
};

enum class EventType : uint8_t {
  Trigger,
  Volume,
  Pitch
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

  double position = 0.0;
  double increment = 1.0;

  float volume = 1.0f;

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

  bool setVolume(int instrument, float value) {
    return queue.push({
	EventType::Volume,
	instrument,
	value
      });
  }

  bool setPitch(int instrument, float value) {
    return queue.push({
	EventType::Pitch,
	instrument,
	value
      });
  }

private:
  jack_client_t* client = nullptr;

  jack_port_t* leftPort = nullptr;
  jack_port_t* rightPort = nullptr;

  jack_port_t* midiInputPort = nullptr;  

  uint32_t sampleRate = 44100;

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
      int index = event.instrument;

      if (index < 0 ||
	  index >= NUM_INSTRUMENTS) {
	continue;
      }

      switch (event.type) {
      case EventType::Trigger:
	startVoice(index);
	break;

      case EventType::Volume:
	volumes[index] = std::clamp(
				    event.value, 0.0f, 2.0f);
	break;

      case EventType::Pitch:
	pitches[index] = std::clamp(
				    event.value, 0.25f, 4.0f);
	break;
      }
    }
  }
  
  void startVoice(int instrument, float velocity = 1.0f) {

    const Sample& sample =
      instruments[instrument].sample;

    if (sample.frames == 0) {
      return;
    }

    Voice* selected = nullptr;

    for (auto& voice : voices) {
      if (!voice.active) {
	selected = &voice;
	break;
      }
    }

    // Steal the oldest voice if necessary.
    if (!selected) {
      selected = &voices[0];
    }

    selected->sample = &sample;
    selected->position = 0.0;

    selected->increment =
      (static_cast<double>(sample.sampleRate) /
       sampleRate) * pitches[instrument];

    selected->volume =
      volumes[instrument] * velocity;  
    selected->active = true;
  }

  int process(jack_nframes_t nframes) {
    float* outL = static_cast<float*>(
				      jack_port_get_buffer(leftPort, nframes));

    float* outR = static_cast<float*>(
				      jack_port_get_buffer(rightPort, nframes));

    void* midiBuffer =
      jack_port_get_buffer(midiInputPort, nframes);

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

	if (jack_midi_event_get(
				&event, midiBuffer, midiIndex) != 0) {
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

	  // MIDI note-on.
	  if (status == 0x90 && velocity > 0) {
	    int instrument =
	      static_cast<int>(note) -
	      MIDI_BASE_NOTE;

	    if (instrument >= 0 &&
		instrument < MIDI_NOTE_COUNT) {

	      float normalizedVelocity =
		velocity / 127.0f;

	      startVoice(
			 instrument,
			 normalizedVelocity
			 );
	    }
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

	float left = interpolate(0);

	float right = sample.channels == 2
	  ? interpolate(1)
	  : left;

	outL[frame] += left * voice.volume;
	outR[frame] += right * voice.volume;

	voice.position += voice.increment;
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
	    << " vol <instrument> <0.0-2.0>\n"
	    << " pitch <instrument> <0.25-4.0>\n"
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

    if (command == "hit") {
      if (input >> instrument) {
	if (!engine.trigger(instrument)) {
	  std::cerr << "Event queue full\n";
	}
      }
    }

    else if (command == "vol") {
      if (input >> instrument >> value) {
	engine.setVolume(instrument, value);
      }
    }

    else if (command == "pitch") {
      if (input >> instrument >> value) {
	engine.setPitch(instrument, value);
      }
    }
  }

  engine.stop();

  return 0;
}
