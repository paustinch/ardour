#include <vector>
#include <cmath>
#include <fstream>

#include <glibmm.h>
#include <pbd/xml++.h>

#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CFString.h>
#else
#include <alsa/asoundlib.h>
#endif

#include <ardour/profile.h>
#include <jack/jack.h>

#include <gtkmm/stock.h>
#include <gtkmm2ext/utils.h>

#include <pbd/convert.h>
#include <pbd/error.h>

#ifdef __APPLE
#include <CFBundle.h>
#endif

#include "engine_dialog.h"
#include "i18n.h"

using namespace std;
using namespace Gtk;
using namespace Gtkmm2ext;
using namespace PBD;
using namespace Glib;

EngineControl::EngineControl ()
	: periods_adjustment (2, 2, 16, 1, 2),
	  periods_spinner (periods_adjustment),
	  priority_adjustment (60, 10, 90, 1, 10),
	  priority_spinner (priority_adjustment),
	  ports_adjustment (128, 8, 1024, 1, 16),
	  ports_spinner (ports_adjustment),
	  realtime_button (_("Realtime")),
	  no_memory_lock_button (_("Do not lock memory")),
	  unlock_memory_button (_("Unlock memory")),
	  soft_mode_button (_("No zombies")),
	  monitor_button (_("Provide monitor ports")),
	  force16bit_button (_("Force 16 bit")),
	  hw_monitor_button (_("H/W monitoring")),
	  hw_meter_button (_("H/W metering")),
	  verbose_output_button (_("Verbose output")),
	  start_button (_("Start")),
	  stop_button (_("Stop")),
	  basic_packer (8, 2),
#ifdef __APPLE__
	  options_packer (4, 2),
#else
	  options_packer (14, 2),
#endif	  
	  device_packer (3, 2)
{
	using namespace Notebook_Helpers;
	Label* label;
	vector<string> strings;

	_used = false;

	strings.push_back (_("8000Hz"));
	strings.push_back (_("22050Hz"));
	strings.push_back (_("44100Hz"));
	strings.push_back (_("48000Hz"));
	strings.push_back (_("88200Hz"));
	strings.push_back (_("96000Hz"));
	strings.push_back (_("192000Hz"));
	set_popdown_strings (sample_rate_combo, strings);
	sample_rate_combo.set_active_text ("48000Hz");

	strings.clear ();
	strings.push_back ("32");
	strings.push_back ("64");
	strings.push_back ("128");
	strings.push_back ("256");
	strings.push_back ("512");
	strings.push_back ("1024");
	strings.push_back ("2048");
	strings.push_back ("4096");
	strings.push_back ("8192");
	set_popdown_strings (period_size_combo, strings);
	period_size_combo.set_active_text ("1024");

	strings.clear ();
	strings.push_back (_("None"));
	strings.push_back (_("Triangular"));
	strings.push_back (_("Rectangular"));
	strings.push_back (_("Shaped"));
	set_popdown_strings (dither_mode_combo, strings);
	dither_mode_combo.set_active_text (_("None"));

	/* basic parameters */

	basic_packer.set_spacings (6);

	strings.clear ();
#ifdef __APPLE__
	strings.push_back (X_("CoreAudio"));
#else
	strings.push_back (X_("ALSA"));
	strings.push_back (X_("OSS"));
	strings.push_back (X_("FFADO"));
#endif
	strings.push_back (X_("NetJACK"));
	strings.push_back (X_("Dummy"));
	set_popdown_strings (driver_combo, strings);
	driver_combo.set_active_text (strings.front());

	/* figure out available devices and set up interface_combo */

	enumerate_devices ();
	driver_combo.signal_changed().connect (mem_fun (*this, &EngineControl::driver_changed));
	driver_changed ();

	strings.clear ();
	strings.push_back (_("Playback/Recording on 1 Device"));
	strings.push_back (_("Playback/Recording on 2 Devices"));
	strings.push_back (_("Playback only"));
	strings.push_back (_("Recording only"));
	set_popdown_strings (audio_mode_combo, strings);
	audio_mode_combo.set_active_text (strings.front());

	audio_mode_combo.signal_changed().connect (mem_fun (*this, &EngineControl::audio_mode_changed));
	audio_mode_changed ();

	label = manage (new Label (_("Driver")));
	basic_packer.attach (*label, 0, 1, 0, 1, FILL|EXPAND, (AttachOptions) 0);
	basic_packer.attach (driver_combo, 1, 2, 0, 1, FILL|EXPAND, (AttachOptions) 0);

	label = manage (new Label (_("Interface")));
	basic_packer.attach (*label, 0, 1, 1, 2, FILL|EXPAND, (AttachOptions) 0);
	basic_packer.attach (interface_combo, 1, 2, 1, 2, FILL|EXPAND, (AttachOptions) 0);

	label = manage (new Label (_("Sample Rate")));
	basic_packer.attach (*label, 0, 1, 2, 3, FILL|EXPAND, (AttachOptions) 0);
	basic_packer.attach (sample_rate_combo, 1, 2, 2, 3, FILL|EXPAND, (AttachOptions) 0);

	label = manage (new Label (_("Buffer size")));
	basic_packer.attach (*label, 0, 1, 3, 4, FILL|EXPAND, (AttachOptions) 0);
	basic_packer.attach (period_size_combo, 1, 2, 3, 4, FILL|EXPAND, (AttachOptions) 0);

	label = manage (new Label (_("Number of buffers")));
	basic_packer.attach (*label, 0, 1, 4, 5, FILL|EXPAND, (AttachOptions) 0);
	basic_packer.attach (periods_spinner, 1, 2, 4, 5, FILL|EXPAND, (AttachOptions) 0);
	periods_spinner.set_value (2);

	label = manage (new Label (_("Approximate latency")));
	label->set_alignment (0.0, 0.5);
	basic_packer.attach (*label, 0, 1, 5, 6, FILL|EXPAND, (AttachOptions) 0);
	basic_packer.attach (latency_label, 1, 2, 5, 6, FILL|EXPAND, (AttachOptions) 0);

	sample_rate_combo.signal_changed().connect (mem_fun (*this, &EngineControl::redisplay_latency));
	periods_adjustment.signal_value_changed().connect (mem_fun (*this, &EngineControl::redisplay_latency));
	period_size_combo.signal_changed().connect (mem_fun (*this, &EngineControl::redisplay_latency));
	redisplay_latency();

	label = manage (new Label (_("Audio Mode")));
	basic_packer.attach (*label, 0, 1, 6, 7, FILL|EXPAND, (AttachOptions) 0);
	
	/* no audio mode with CoreAudio, its duplex or nuthin' */

#ifndef __APPLE__
	basic_packer.attach (audio_mode_combo, 1, 2, 6, 7, FILL|EXPAND, (AttachOptions) 0);
#endif

	/*

	if (engine_running()) {
		start_button.set_sensitive (false);
	} else {
		stop_button.set_sensitive (false);
	}

	start_button.signal_clicked().connect (mem_fun (*this, &EngineControl::start_engine));
	stop_button.signal_clicked().connect (mem_fun (*this, &EngineControl::start_engine));
	*/

	button_box.pack_start (start_button, false, false);
	button_box.pack_start (stop_button, false, false);

	// basic_packer.attach (button_box, 0, 2, 8, 9, FILL|EXPAND, (AttachOptions) 0);

	/* options */

	int row = 0;
	options_packer.set_spacings (6);

	options_packer.attach (realtime_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	label = manage (new Label (_("Realtime Priority")));
	label->set_alignment (0.0, 0.5);
	options_packer.attach (priority_spinner, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	options_packer.attach (*label, 1, 2, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	priority_spinner.set_value (60);

	realtime_button.signal_toggled().connect (mem_fun (*this, &EngineControl::realtime_changed));
	realtime_changed ();

#ifndef __APPLE__
	options_packer.attach (no_memory_lock_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	options_packer.attach (unlock_memory_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	options_packer.attach (soft_mode_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	options_packer.attach (monitor_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	options_packer.attach (force16bit_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	options_packer.attach (hw_monitor_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	options_packer.attach (hw_meter_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
	options_packer.attach (verbose_output_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
#else 
	options_packer.attach (verbose_output_button, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
#endif

	strings.clear ();
	strings.push_back (_("Ignore"));
	strings.push_back ("500 msec");
	strings.push_back ("1 sec");
	strings.push_back ("2 sec");
	strings.push_back ("10 sec");
	set_popdown_strings (timeout_combo, strings);
	timeout_combo.set_active_text (strings.front ());

	label = manage (new Label (_("Client timeout")));
	label->set_alignment (0.0, 0.5);
	options_packer.attach (timeout_combo, 0, 1, row, row + 1, FILL|EXPAND, AttachOptions(0));
	options_packer.attach (*label, 1, 2, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;

	label = manage (new Label (_("Number of ports")));
	label->set_alignment (0.0, 0.5);
	options_packer.attach (ports_spinner, 0, 1, row, row + 1, FILL|EXPAND, AttachOptions(0));
	options_packer.attach (*label, 1, 2, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;

#ifndef __APPLE__
	label = manage (new Label (_("Dither")));	
	label->set_alignment (0.0, 0.5);
	options_packer.attach (dither_mode_combo, 0, 1, row, row + 1, FILL|EXPAND, AttachOptions(0));
	options_packer.attach (*label, 1, 2, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
	++row;
#endif

	strings.clear ();

	find_jack_servers (strings);

	if (strings.empty()) {
		fatal << _("No JACK server found anywhere on this system. Please install JACK and restart") << endmsg;
		/*NOTREACHED*/
	}
	
	set_popdown_strings (serverpath_combo, strings);
	serverpath_combo.set_active_text (strings.front());

	if (strings.size() > 1) {
		label = manage (new Label (_("Server:")));
		options_packer.attach (*label, 0, 1, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
		label->set_alignment (0.0, 0.5);
		options_packer.attach (serverpath_combo, 1, 2, row, row + 1, FILL|EXPAND, (AttachOptions) 0);
		++row;
	}

	/* device settings */

	device_packer.set_spacings (6);

	label = manage (new Label (_("Input device")));
	label->set_alignment (1.0, 0.5);
	device_packer.attach (*label, 0, 1, 0, 1, FILL|EXPAND, (AttachOptions) 0);
	device_packer.attach (input_device_combo, 1, 2, 0, 1, FILL|EXPAND, (AttachOptions) 0);
	label = manage (new Label (_("Output device")));
	label->set_alignment (1.0, 0.5);
	device_packer.attach (*label, 0, 1, 1, 2, FILL|EXPAND, (AttachOptions) 0);
	device_packer.attach (output_device_combo, 1, 2, 1, 2, FILL|EXPAND, (AttachOptions) 0);	
	label = manage (new Label (_("Input channels")));
	label->set_alignment (1.0, 0.5);
	device_packer.attach (*label, 0, 1, 2, 3, FILL|EXPAND, (AttachOptions) 0);
	device_packer.attach (input_channels, 1, 2, 2, 3, FILL|EXPAND, (AttachOptions) 0);
	label = manage (new Label (_("Output channels")));
	label->set_alignment (1.0, 0.5);
	device_packer.attach (*label, 0, 1, 3, 4, FILL|EXPAND, (AttachOptions) 0);
	device_packer.attach (output_channels, 1, 2, 3, 4, FILL|EXPAND, (AttachOptions) 0);
	label = manage (new Label (_("Hardware input latency (samples)")));
	label->set_alignment (1.0, 0.5);
	device_packer.attach (*label, 0, 1, 4, 5, FILL|EXPAND, (AttachOptions) 0);
	device_packer.attach (input_latency, 1, 2, 4, 5, FILL|EXPAND, (AttachOptions) 0);
	label = manage (new Label (_("Hardware output latency (samples)")));
	label->set_alignment (1.0, 0.5);
	device_packer.attach (*label, 0, 1, 5, 6, FILL|EXPAND, (AttachOptions) 0);
	device_packer.attach (output_latency, 1, 2, 5, 6, FILL|EXPAND, (AttachOptions) 0);

	basic_hbox.pack_start (basic_packer, false, false);

	notebook.pages().push_back (TabElem (basic_hbox, _("Basics")));
	notebook.pages().push_back (TabElem (options_packer, _("Options")));
	notebook.pages().push_back (TabElem (device_packer, _("Device Parameters")));

	set_border_width (12);
	pack_start (notebook);

}

EngineControl::~EngineControl ()
{

}

void
EngineControl::build_command_line (vector<string>& cmd)
{
	string str;
	string driver;
	bool using_oss = false;
	bool using_alsa = false;
	bool using_coreaudio = false;
	bool using_netjack = false;
	bool using_ffado = false;

	/* first, path to jackd */

	cmd.push_back (serverpath_combo.get_active_text ());
	
	/* now jackd arguments */

	str = timeout_combo.get_active_text ();
	if (str != _("Ignore")) {
		double secs;
		uint32_t msecs;
		atof (str);
		msecs = (uint32_t) floor (secs * 1000.0);
		cmd.push_back ("-t");
		cmd.push_back (to_string (msecs, std::dec));
	}

	if (no_memory_lock_button.get_active()) {
		cmd.push_back ("-m"); /* no munlock */
	}
	
	cmd.push_back ("-p"); /* port max */
	cmd.push_back (to_string ((uint32_t) floor (ports_spinner.get_value()), std::dec));

	if (realtime_button.get_active()) {
		cmd.push_back ("-R");
		cmd.push_back ("-P");
		cmd.push_back (to_string ((uint32_t) floor (priority_spinner.get_value()), std::dec));
	}

	if (unlock_memory_button.get_active()) {
		cmd.push_back ("-u");
	}

	if (verbose_output_button.get_active()) {
		cmd.push_back ("-v");
	}
	
	/* now add fixed arguments (not user-selectable) */

	cmd.push_back ("-T"); // temporary */

	/* next the driver */

	cmd.push_back ("-d");

	driver = driver_combo.get_active_text ();
	if (driver == X_("ALSA")) {
		using_alsa = true;
		cmd.push_back ("alsa");
	} else if (driver == X_("OSS")) {
		using_oss = true;
		cmd.push_back ("oss");
	} else if (driver == X_("CoreAudio")) {
		using_coreaudio = true;
		cmd.push_back ("coreaudio");
	} else if (driver == X_("NetJACK")) {
		using_netjack = true;
		cmd.push_back ("netjack");
	} else if (driver == X_("FFADO")) {
		using_ffado = true;
		cmd.push_back ("ffado");
	}

	/* driver arguments */

	if (!using_coreaudio) {
		str = audio_mode_combo.get_active_text();
		
		if (str == _("Playback/Recording on 1 Device")) {
			
			/* relax */
			
		} else if (str == _("Playback/Recording on 2 Devices")) {
			
			cmd.push_back ("-C");
			cmd.push_back (get_device_name (driver, input_device_combo.get_active_text()));
			cmd.push_back ("-P");
			cmd.push_back (get_device_name (driver, output_device_combo.get_active_text()));
			
		} else if (str == _("Playback only")) {
			cmd.push_back ("-P");
		} else if (str == _("Recording only")) {
			cmd.push_back ("-C");
		}

		cmd.push_back ("-n");
		cmd.push_back (to_string ((uint32_t) floor (periods_spinner.get_value()), std::dec));
	}

	cmd.push_back ("-r");
	cmd.push_back (to_string (get_rate(), std::dec));
	
	cmd.push_back ("-p");
	cmd.push_back (period_size_combo.get_active_text());

	if (using_alsa) {
		
		if (audio_mode_combo.get_active_text() != _("Playback/Recording on 2 Devices")) {
			cmd.push_back ("-d");
			cmd.push_back (get_device_name (driver, interface_combo.get_active_text()));
		} 

		if (hw_meter_button.get_active()) {
			cmd.push_back ("-M");
		}
		
		if (hw_monitor_button.get_active()) {
			cmd.push_back ("-H");
		}

		str = dither_mode_combo.get_active_text();

		if (str == _("None")) {
		} else if (str == _("Triangular")) {
			cmd.push_back ("-z triangular");
		} else if (str == _("Rectangular")) {
			cmd.push_back ("-z rectangular");
		} else if (str == _("Shaped")) {
			cmd.push_back ("-z shaped");
		}

		if (force16bit_button.get_active()) {
			cmd.push_back ("-S");
		}
		
		if (soft_mode_button.get_active()) {
			cmd.push_back ("-s");
		}

	} else if (using_coreaudio) {

#ifdef __APPLE__
		cmd.push_back ("-n");
		cmd.push_back (get_device_name (driver, interface_combo.get_active_text()));
#endif

	} else if (using_oss) {

	} else if (using_netjack) {

	}
}

bool
EngineControl::engine_running ()
{
	jack_status_t status;
	jack_client_t* c = jack_client_open ("ardourprobe", JackNoStartServer, &status);

	if (status == 0) {
		jack_client_close (c);
		return true;
	}
	return false;
}

int
EngineControl::start_engine ()
{
	vector<string> args;
	std::string cwd = "/tmp";
	int ret = 0;

	build_command_line (args);

	Glib::ustring jackdrc_path = Glib::get_home_dir();
	jackdrc_path += "/.jackdrc";

	ofstream jackdrc (jackdrc_path.c_str());
	if (!jackdrc) {
		error << string_compose (_("cannot open JACK rc file %1 to store parameters"), jackdrc_path) << endmsg;
		return -1;
	}

	cerr << "will execute ...\n";
	for (vector<string>::iterator i = args.begin(); i != args.end(); ++i) {
		jackdrc << (*i) << ' ';
		cerr << (*i) << ' ';
	}
	jackdrc << endl;
	cerr << endl;
	jackdrc.close ();

	_used = true;
	
#if 0

	try {
		spawn_async_with_pipes (cwd, args, SpawnFlags (0), sigc::slot<void>(), &engine_pid, &engine_stdin, &engine_stdout, &engine_stderr);
	}
	
	catch (Glib::Exception& err) {
		error << _("could not start JACK server: ") << err.what() << endmsg;
		ret = -1;
	}
#endif

	return ret;
}

int
EngineControl::stop_engine ()
{
	return 0;
}

void
EngineControl::realtime_changed ()
{
	priority_spinner.set_sensitive (realtime_button.get_active());
}

void
EngineControl::enumerate_devices ()
{
	/* note: case matters for the map keys */

#ifdef __APPLE__
	devices["CoreAudio"] = enumerate_coreaudio_devices ();
#else
	devices["ALSA"] = enumerate_alsa_devices ();
	devices["FFADO"] = enumerate_ffado_devices ();
	devices["OSS"] = enumerate_oss_devices ();
	devices["Dummy"] = enumerate_dummy_devices ();
	devices["NetJACK"] = enumerate_netjack_devices ();
#endif
}

#ifdef __APPLE__
static OSStatus 
getDeviceUIDFromID( AudioDeviceID id, char *name, size_t nsize)
{
	UInt32 size = sizeof(CFStringRef);
	CFStringRef UI;
	OSStatus res = AudioDeviceGetProperty(id, 0, false,
		kAudioDevicePropertyDeviceUID, &size, &UI);
	if (res == noErr) 
		CFStringGetCString(UI,name,nsize,CFStringGetSystemEncoding());
	CFRelease(UI);
	return res;
}

vector<string>
EngineControl::enumerate_coreaudio_devices ()
{
	vector<string> devs;
	
	// Find out how many Core Audio devices are there, if any...
	// (code snippet gently "borrowed" from St?hane Letz jackdmp;)
	OSStatus err;
	Boolean isWritable;
	size_t outSize = sizeof(isWritable);

	backend_devs.clear ();

	err = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices,
					   &outSize, &isWritable);
	if (err == noErr) {
		// Calculate the number of device available...
		int numCoreDevices = outSize / sizeof(AudioDeviceID);
		// Make space for the devices we are about to get...
		AudioDeviceID *coreDeviceIDs = new AudioDeviceID [numCoreDevices];
		err = AudioHardwareGetProperty(kAudioHardwarePropertyDevices,
					       &outSize, (void *) coreDeviceIDs);
		if (err == noErr) {
			// Look for the CoreAudio device name...
			char coreDeviceName[256];
			size_t nameSize = sizeof (coreDeviceName);
			for (int i = 0; i < numCoreDevices; i++) {
				err = AudioDeviceGetPropertyInfo(coreDeviceIDs[i],
								 0, true, kAudioDevicePropertyDeviceName,
								 &outSize, &isWritable);
				if (err == noErr) {
					err = AudioDeviceGetProperty(coreDeviceIDs[i],
								     0, true, kAudioDevicePropertyDeviceName,
								     &nameSize, (void *) coreDeviceName);
					if (err == noErr) {

						char drivername[128];

						// this returns the unique id for the device
						// that must be used on the commandline for jack
						
						if (getDeviceUIDFromID(coreDeviceIDs[i], drivername, sizeof (drivername)) == noErr) {
							devs.push_back (coreDeviceName);
							backend_devs.push_back (drivername);
						} 
					}
				}
			}
		}
		delete [] coreDeviceIDs;
	}

	return devs;
}
#else
vector<string>
EngineControl::enumerate_alsa_devices ()
{
	vector<string> devs;

	snd_ctl_t *handle;
	snd_ctl_card_info_t *info;
	snd_pcm_info_t *pcminfo;
	snd_ctl_card_info_alloca(&info);
	snd_pcm_info_alloca(&pcminfo);
	string devname;
	int cardnum = -1;
	int device = -1;

	backend_devs.clear ();

	while (snd_card_next (&cardnum) >= 0 && cardnum >= 0) {

		devname = "hw:";
		devname += to_string (cardnum, std::dec);

		if (snd_ctl_open (&handle, devname.c_str(), 0) >= 0 && snd_ctl_card_info (handle, info) >= 0) {

			while (snd_ctl_pcm_next_device (handle, &device) >= 0 && device >= 0) {

				bool have_playback = false;
				bool have_capture = false;

				/* find duplex devices only */

				snd_pcm_info_set_device (pcminfo, device);
				snd_pcm_info_set_subdevice (pcminfo, 0);
				snd_pcm_info_set_stream (pcminfo, SND_PCM_STREAM_CAPTURE);

				if (snd_ctl_pcm_info (handle, pcminfo) >= 0) {
					have_capture = true;
				}

				snd_pcm_info_set_device (pcminfo, device);
				snd_pcm_info_set_subdevice (pcminfo, 0);
				snd_pcm_info_set_stream (pcminfo, SND_PCM_STREAM_PLAYBACK);

				if (snd_ctl_pcm_info (handle, pcminfo) >= 0) {
					have_playback = true;
				}

				if (have_capture && have_playback) {
					devs.push_back (snd_pcm_info_get_name (pcminfo));
					devname += ',';
					devname += to_string (device, std::dec);
					backend_devs.push_back (devname);
				}
			}

			snd_ctl_close(handle);
		}
	}

	return devs;
}

vector<string>
EngineControl::enumerate_ffado_devices ()
{
	vector<string> devs;
	return devs;
}
vector<string>
EngineControl::enumerate_oss_devices ()
{
	vector<string> devs;
	return devs;
}
vector<string>
EngineControl::enumerate_dummy_devices ()
{
	vector<string> devs;
	return devs;
}
vector<string>
EngineControl::enumerate_netjack_devices ()
{
	vector<string> devs;
	return devs;
}
#endif

void
EngineControl::driver_changed ()
{
	string driver = driver_combo.get_active_text();
	vector<string>& strings = devices[driver];
	
	set_popdown_strings (interface_combo, strings);
	set_popdown_strings (input_device_combo, strings);
	set_popdown_strings (output_device_combo, strings);

	if (!strings.empty()) {
		interface_combo.set_active_text (strings.front());
		input_device_combo.set_active_text (strings.front());
		output_device_combo.set_active_text (strings.front());
	}
	
	if (driver == "ALSA") {
		soft_mode_button.set_sensitive (true);
		force16bit_button.set_sensitive (true);
		hw_monitor_button.set_sensitive (true);
		hw_meter_button.set_sensitive (true);
		monitor_button.set_sensitive (true);
	} else {
		soft_mode_button.set_sensitive (false);
		force16bit_button.set_sensitive (false);
		hw_monitor_button.set_sensitive (false);
		hw_meter_button.set_sensitive (false);
		monitor_button.set_sensitive (false);
	}
}

uint32_t
EngineControl::get_rate ()
{
	return atoi (sample_rate_combo.get_active_text ());
}

void
EngineControl::redisplay_latency ()
{
	uint32_t rate = get_rate();
	float periods = periods_adjustment.get_value();
	float period_size = atof (period_size_combo.get_active_text());

	char buf[32];
	snprintf (buf, sizeof(buf), "%.1fmsec", (periods * period_size) / (rate/1000.0));

	latency_label.set_text (buf);
}

void
EngineControl::audio_mode_changed ()
{
	Glib::ustring str = audio_mode_combo.get_active_text();

	if (str == _("Playback/Recording on 1 Device")) {
		input_device_combo.set_sensitive (false);
		output_device_combo.set_sensitive (false);
	} else if (str == _("Playback/Recording on 2 Devices")) {
		input_device_combo.set_sensitive (true);
		output_device_combo.set_sensitive (true);
	} else if (str == _("Playback only")) {
		output_device_combo.set_sensitive (true);
	} else if (str == _("Recording only")) {
		input_device_combo.set_sensitive (true);
	}
}

void
EngineControl::find_jack_servers (vector<string>& strings)
{
#ifdef __APPLE__
	if (Profile->get_single_package()) {

		/* this magic lets us finds the path to the OSX bundle, and then
		   we infer JACK's location from there
		*/
		
		CFURLRef pluginRef = CFBundleCopyBundleURL(CFBundleGetMainBundle());
		CFStringRef macPath = CFURLCopyFileSystemPath(pluginRef, 
							      kCFURLPOSIXPathStyle);
		std::string path = CFStringGetCStringPtr(macPath, 
							 CFStringGetSystemEncoding());
		CFRelease(pluginRef);
		CFRelease(macPath);
		
		path += '/jackd';

		if (Glib::file_test (path, FILE_TEST_EXISTS)) {
			strings.push_back ();
		} else {
			warning << _("JACK appears to be missing from the Ardour bundle") << endmsg;
		}
	}
#endif
	
	if (Glib::file_test ("/usr/bin/jackd", FILE_TEST_EXISTS)) {
		strings.push_back ("/usr/bin/jackd");
	}
	if (Glib::file_test ("/usr/local/bin/jackd", FILE_TEST_EXISTS)) {
		strings.push_back ("/usr/local/bin/jackd");
	}
	if (Glib::file_test ("/opt/bin/jackd", FILE_TEST_EXISTS)) {
		strings.push_back ("/opt/bin/jackd");
	}
	if (Glib::file_test ("/usr/bin/jackdmp", FILE_TEST_EXISTS)) {
		strings.push_back ("/usr/bin/jackd");
	}
	if (Glib::file_test ("/usr/local/bin/jackdmp", FILE_TEST_EXISTS)) {
		strings.push_back ("/usr/local/bin/jackd");
	}
	if (Glib::file_test ("/opt/bin/jackdmp", FILE_TEST_EXISTS)) {
		strings.push_back ("/opt/bin/jackd");
	}

}

string
EngineControl::get_device_name (const string& driver, const string& human_readable)
{
	vector<string>::iterator n;
	vector<string>::iterator i;

	if (backend_devs.empty()) {
		return human_readable;
	}
	
	for (i = devices[driver].begin(), n = backend_devs.begin(); i != devices[driver].end(); ++i, ++n) {
		if (human_readable == (*i)) {
			return (*n);
		}
	}
	
	if (i == devices[driver].end()) {
		fatal << string_compose (_("programming error: %1"), "true hardware name for ID missing") << endmsg;
		/*NOTREACHED*/
	}

	/* keep gcc happy */

	return string();
}

XMLNode&
EngineControl::get_state ()
{
	XMLNode* root = new XMLNode ("AudioSetup");
	XMLNode* child;
	Glib::ustring path;

	child = new XMLNode ("periods");
	child->add_property ("val", to_string (periods_adjustment.get_value(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("priority");
	child->add_property ("val", to_string (priority_adjustment.get_value(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("ports");
	child->add_property ("val", to_string (ports_adjustment.get_value(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("inchannels");
	child->add_property ("val", to_string (input_channels.get_value(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("outchannels");
	child->add_property ("val", to_string (output_channels.get_value(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("inlatency");
	child->add_property ("val", to_string (input_latency.get_value(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("outlatency");
	child->add_property ("val", to_string (output_latency.get_value(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("realtime");
	child->add_property ("val", to_string (realtime_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("nomemorylock");
	child->add_property ("val", to_string (no_memory_lock_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("unlockmemory");
	child->add_property ("val", to_string (unlock_memory_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("softmode");
	child->add_property ("val", to_string (soft_mode_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("force16bit");
	child->add_property ("val", to_string (force16bit_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("hwmonitor");
	child->add_property ("val", to_string (hw_monitor_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("hwmeter");
	child->add_property ("val", to_string (hw_meter_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("verbose");
	child->add_property ("val", to_string (verbose_output_button.get_active(), std::dec));
	root->add_child_nocopy (*child);

	child = new XMLNode ("samplerate");
	child->add_property ("val", sample_rate_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("periodsize");
	child->add_property ("val", period_size_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("serverpath");
	child->add_property ("val", serverpath_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("driver");
	child->add_property ("val", driver_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("interface");
	child->add_property ("val", interface_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("timeout");
	child->add_property ("val", timeout_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("dither");
	child->add_property ("val", dither_mode_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("audiomode");
	child->add_property ("val", audio_mode_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("inputdevice");
	child->add_property ("val", input_device_combo.get_active_text());
	root->add_child_nocopy (*child);

	child = new XMLNode ("outputdevice");
	child->add_property ("val", output_device_combo.get_active_text());
	root->add_child_nocopy (*child);
	
	return *root;
}

void
EngineControl::set_state (const XMLNode& root)
{
	XMLNodeList          clist;
	XMLNodeConstIterator citer;
	XMLNode* child;
	XMLProperty* prop;

	int val;
	string strval;

	clist = root.children();

	for (citer = clist.begin(); citer != clist.end(); ++citer) {

		child = *citer;

		prop = child->property ("val");

		if (!prop || prop->value().empty()) {
			error << string_compose (_("AudioSetup value for %1 is missing data"), child->name()) << endmsg;
			continue;
		}
		
		strval = prop->value();

		/* adjustments/spinners */

		if (child->name() == "periods") {
			val = atoi (strval);
			periods_adjustment.set_value(val);
		} else if (child->name() == "priority") {
			val = atoi (strval);
			priority_adjustment.set_value(val);
		} else if (child->name() == "ports") {
			val = atoi (strval);
			ports_adjustment.set_value(val);
		} else if (child->name() == "inchannels") {
			val = atoi (strval);
			input_channels.set_value(val);
		} else if (child->name() == "outchannels") {
			val = atoi (strval);
			output_channels.set_value(val);
		} else if (child->name() == "inlatency") {
			val = atoi (strval);
			input_latency.set_value(val);
		} else if (child->name() == "outlatency") {
			val = atoi (strval);
			output_latency.set_value(val);
		}

		/* buttons */

		else if (child->name() == "realtime") {
			val = atoi (strval);
			realtime_button.set_active(val);
		} else if (child->name() == "nomemorylock") {
			val = atoi (strval);
			no_memory_lock_button.set_active(val);
		} else if (child->name() == "unlockmemory") {
			val = atoi (strval);
			unlock_memory_button.set_active(val);
		} else if (child->name() == "softmode") {
			val = atoi (strval);
			soft_mode_button.set_active(val);
		} else if (child->name() == "force16bit") {
			val = atoi (strval);
			force16bit_button.set_active(val);
		} else if (child->name() == "hwmonitor") {
			val = atoi (strval);
			hw_monitor_button.set_active(val);
		} else if (child->name() == "hwmeter") {
			val = atoi (strval);
			hw_meter_button.set_active(val);
		} else if (child->name() == "verbose") {
			val = atoi (strval);
			verbose_output_button.set_active(val);
		}

		/* combos */

		else if (child->name() == "samplerate") {
			sample_rate_combo.set_active_text(strval);
		} else if (child->name() == "periodsize") {
			period_size_combo.set_active_text(strval);
		} else if (child->name() == "serverpath") {
			serverpath_combo.set_active_text(strval);
		} else if (child->name() == "driver") {
			driver_combo.set_active_text(strval);
		} else if (child->name() == "interface") {
			interface_combo.set_active_text(strval);
		} else if (child->name() == "timeout") {
			timeout_combo.set_active_text(strval);
		} else if (child->name() == "dither") {
			dither_mode_combo.set_active_text(strval);
		} else if (child->name() == "audiomode") {
			audio_mode_combo.set_active_text(strval);
		} else if (child->name() == "inputdevice") {
			input_device_combo.set_active_text(strval);
		} else if (child->name() == "outputdevice") {
			output_device_combo.set_active_text(strval);
		}
	}
}
