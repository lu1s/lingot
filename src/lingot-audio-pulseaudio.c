/*
 * lingot, a musical instrument tuner.
 *
 * Copyright (C) 2004-2013  Ibán Cereijo Graña.
 * Copyright (C) 2004-2008  Jairo Chapela Martínez.

 *
 * This file is part of lingot.
 *
 * lingot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * lingot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lingot; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>

#include "lingot-defs.h"
#include "lingot-audio-pulseaudio.h"
#include "lingot-i18n.h"
#include "lingot-msg.h"

#ifdef PULSEAUDIO
#include <pulse/pulseaudio.h>
#endif

#ifdef PULSEAUDIO
static int channels = 1;
static pa_sample_spec ss;
#endif

LingotAudioHandler* lingot_audio_pulseaudio_new(char* device, int sample_rate) {

	LingotAudioHandler* audio = NULL;

#	ifdef PULSEAUDIO

	audio = malloc(sizeof(LingotAudioHandler));
	audio->pa_client = 0;
	strcpy(audio->device, "");
	audio->read_buffer = NULL;

//	sample_rate = 44100;
	audio->real_sample_rate = sample_rate;

	audio->audio_system = AUDIO_SYSTEM_PULSEAUDIO;
	if (sample_rate >= 44100) {
		audio->read_buffer_size_samples = 2048;
	} else if (sample_rate >= 22050) {
		audio->read_buffer_size_samples = 1024;
	} else {
		audio->read_buffer_size_samples = 512;
	}

	int error;

//	ss.format = PA_SAMPLE_S16NE;
	ss.format = PA_SAMPLE_FLOAT32; // TODO: config?
	ss.channels = channels;
	ss.rate = sample_rate;

	printf("sr %i, real sr %i, format = %i\n", ss.rate, audio->real_sample_rate,
			ss.format);

	audio->bytes_per_sample = pa_sample_size(&ss);
	audio->read_buffer_size_bytes = channels * audio->read_buffer_size_samples
			* audio->bytes_per_sample;

	pa_buffer_attr buff;
	buff.maxlength = -1;
	buff.fragsize = audio->read_buffer_size_bytes;

	const char* device_name = device;
	if (!strcmp(device_name, "default") || !strcmp(device_name, "")) {
		device_name = NULL;
	}

	audio->pa_client = pa_simple_new(NULL, // Use the default server.
			"Lingot", // Our application's name.
			PA_STREAM_RECORD, //
			device_name, //
			"Lingot record thread", // Description of our stream.
			&ss, // sample format.
			NULL, // Use default channel map
			&buff, //
			&error);

	if (!audio->pa_client) {
		char buff[100];
		sprintf(buff, "%s\n%s", _("Error creating PulseAudio client."),
				pa_strerror(error));
		lingot_msg_add_error_with_code(buff, error);
		free(audio);
		audio = NULL;
	} else {
		audio->read_buffer = malloc(audio->read_buffer_size_bytes);
		memset(audio->read_buffer, 0, audio->read_buffer_size_bytes);
	}

#	else
	lingot_msg_add_error(
			_("The application has not been built with PulseAudio support"));
#	endif

	return audio;
}

void lingot_audio_pulseaudio_destroy(LingotAudioHandler* audio) {

#	ifdef PULSEAUDIO
	if (audio != NULL ) {
		if (audio->pa_client != 0x0) {
			pa_simple_free(audio->pa_client);
			audio->pa_client = 0x0;
		}
	}
#	endif
}

int lingot_audio_pulseaudio_read(LingotAudioHandler* audio) {

	int samples_read = -1;
#	ifdef PULSEAUDIO
	int error;

	const size_t sample_size = pa_sample_size(&ss);
	const unsigned int block_size = channels * audio->read_buffer_size_samples
			* sample_size;

	int result = pa_simple_read(audio->pa_client, audio->read_buffer,
			block_size, &error);

//	printf("result = %i\n", result);
	if (result < 0) {
		char buff[100];
		sprintf(buff, "%s\n%s", _("Read from audio interface failed."),
				pa_strerror(error));
		lingot_msg_add_error_with_code(buff, error);
	} else {

		samples_read = audio->read_buffer_size_samples;
		int i;
		switch (ss.format) {
		case PA_SAMPLE_S16LE: {
			int16_t* read_buffer = (int16_t*) audio->read_buffer;
			for (i = 0; i < samples_read; i++) {
				audio->flt_read_buffer[i] = read_buffer[i];
			}
			break;
		}
		case PA_SAMPLE_FLOAT32: {
			float* read_buffer = (float*) audio->read_buffer;
			for (i = 0; i < samples_read; i++) {
				audio->flt_read_buffer[i] = read_buffer[i] * FLT_SAMPLE_SCALE;
			}
			break;
		}
		default:
			break;
		}

	}

#	endif

	return samples_read;
}

void lingot_audio_pulseaudio_cancel(LingotAudioHandler* audio) {
#ifdef PULSEAUDIO
	// warning: memory leak?
	// TODO: avoid it by using the async API
	audio->pa_client = 0x0;
#endif
}

#ifdef PULSEAUDIO

// linked list struct for storing the capture device names
struct device_name_node_t {
	char* name;
	struct device_name_node_t* next;
};

static pa_context *context = NULL;
static pa_mainloop_api *mainloop_api = NULL;
static pa_proplist *proplist = NULL;

static void lingot_audio_pulseaudio_mainloop_quit(int ret);
static void lingot_audio_pulseaudio_context_drain_complete(pa_context *c,
		void *userdata);
static void lingot_audio_pulseaudio_drain();
static void lingot_audio_pulseaudio_get_source_info_callback(pa_context *c,
		const pa_source_info *i, int is_last, void *userdata);
static void lingot_audio_pulseaudio_context_state_callback(pa_context *c,
		void *userdata);
#endif

LingotAudioSystemProperties* lingot_audio_pulseaudio_get_audio_system_properties(
		audio_system_t audio_system) {

	LingotAudioSystemProperties* properties = NULL;
#	ifdef PULSEAUDIO
	properties = (LingotAudioSystemProperties*) malloc(
			1 * sizeof(LingotAudioSystemProperties));

	properties->forced_sample_rate = 0;
	properties->n_devices = 0;
	properties->devices = NULL;
	properties->n_sample_rates = 5;
	properties->sample_rates = malloc(properties->n_sample_rates * sizeof(int));
	properties->sample_rates[0] = 8000;
	properties->sample_rates[1] = 11025;
	properties->sample_rates[2] = 22050;
	properties->sample_rates[3] = 44100;
	properties->sample_rates[4] = 48000; // TODO

	struct device_name_node_t* device_names_first =
			(struct device_name_node_t*) malloc(
					sizeof(struct device_name_node_t));
	struct device_name_node_t* device_names_last = device_names_first;
	// the first record is the default source
	char buff[100];
	sprintf(buff, "%s <default>", _("Default Source"));
	device_names_first->name = strdup(buff);
	device_names_first->next = NULL;

	context = NULL;
	mainloop_api = NULL;
	proplist = NULL;
	pa_mainloop *m = NULL;
	int ret = 1;
	char *server = NULL;
	int fail = 0;

	proplist = pa_proplist_new();

	if (!(m = pa_mainloop_new())) {
		fprintf(stderr, "PulseAudio: pa_mainloop_new() failed.\n");
	} else {

		mainloop_api = pa_mainloop_get_api(m);

//		//!pa_assert_se(pa_signal_init(mainloop_api) == 0);
//		pa_signal_new(SIGINT, exit_signal_callback, NULL);
//		pa_signal_new(SIGTERM, exit_signal_callback, NULL);
//		pa_disable_sigpipe();

		if (!(context = pa_context_new_with_proplist(mainloop_api, NULL,
				proplist))) {
			fprintf(stderr, "PulseAudio: pa_context_new() failed.\n");
		} else {

			pa_context_set_state_callback(context,
					lingot_audio_pulseaudio_context_state_callback,
					&device_names_last);
			if (pa_context_connect(context, server, 0, NULL ) < 0) {
				fprintf(stderr, "PulseAudio: pa_context_connect() failed: %s",
						pa_strerror(pa_context_errno(context)));
			} else if (pa_mainloop_run(m, &ret) < 0) {
				fprintf(stderr, "PulseAudio: pa_mainloop_run() failed.");
			}
		}
	}

	if (context)
		pa_context_unref(context);

	if (m) {
//		pa_signal_done();
		pa_mainloop_free(m);
	}

	pa_xfree(server);

	if (proplist)
		pa_proplist_free(proplist);

	int device_index;

	if (!fail) {
		struct device_name_node_t* name_node_current;
		for (name_node_current = device_names_first; name_node_current != NULL ;
				name_node_current = name_node_current->next) {
			properties->n_devices++;
		}

		// copy the device names list
		properties->devices = (char**) malloc(
				properties->n_devices * sizeof(char*));
		name_node_current = device_names_first;
		for (device_index = 0; device_index < properties->n_devices;
				device_index++) {
			properties->devices[device_index] = name_node_current->name;
			name_node_current = name_node_current->next;
		}
	} else {
		fprintf(stderr,
				"PulseAudio: cannot obtain device information from server\n");
	}

	// dispose the device names list
	struct device_name_node_t* name_node_current;
	for (name_node_current = device_names_first; name_node_current != NULL ;) {
		struct device_name_node_t* name_node_previous = name_node_current;
		name_node_current = name_node_current->next;
		free(name_node_previous);
	}

#	endif

	return properties;
}

#ifdef PULSEAUDIO

static void lingot_audio_pulseaudio_mainloop_quit(int ret) {
	mainloop_api->quit(mainloop_api, ret);
}

static void lingot_audio_pulseaudio_context_drain_complete(pa_context *c,
		void *userdata) {
	pa_context_disconnect(c);
}

static void lingot_audio_pulseaudio_drain() {
	pa_operation *o;

	if (!(o = pa_context_drain(context,
			lingot_audio_pulseaudio_context_drain_complete, NULL )))
		pa_context_disconnect(context);
	else
		pa_operation_unref(o);
}

static void lingot_audio_pulseaudio_get_source_info_callback(pa_context *c,
		const pa_source_info *i, int is_last, void *userdata) {

	if (is_last < 0) {
		fprintf(stderr, "PulseAudio: failed to get source information: %s",
				pa_strerror(pa_context_errno(c)));
		lingot_audio_pulseaudio_mainloop_quit(1);
		return;
	}

	if (is_last) {
		lingot_audio_pulseaudio_drain();
		return;
	}

	struct device_name_node_t** device_names_last =
			(struct device_name_node_t**) userdata;

	char buff[512];
	sprintf(buff, "%s <%s>", i->description, i->name);

	printf("%s <%s>\n", i->description, i->name);
	printf("\tmonitor of: %s\n", i->monitor_of_sink_name);
	printf("\t%i channels, rate %i, format %i\n", i->sample_spec.channels,
			i->sample_spec.rate, i->sample_spec.format);
	printf("\tlags %i\n", i->flags);

	struct device_name_node_t* new_name_node =
			(struct device_name_node_t*) malloc(
					sizeof(struct device_name_node_t*));
	new_name_node->name = strdup(buff);
	new_name_node->next = NULL;

	(*device_names_last)->next = new_name_node;
	*device_names_last = new_name_node;
}

static void lingot_audio_pulseaudio_context_state_callback(pa_context *c,
		void *userdata) {
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_CONNECTING:
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_SETTING_NAME:
		break;

	case PA_CONTEXT_READY:
		pa_operation_unref(
				pa_context_get_source_info_list(c,
						lingot_audio_pulseaudio_get_source_info_callback,
						userdata));
		break;

	case PA_CONTEXT_TERMINATED:
		lingot_audio_pulseaudio_mainloop_quit(0);
		break;

	case PA_CONTEXT_FAILED:
	default:
		fprintf(stderr, "PulseAudio: connection failure: %s\n",
				pa_strerror(pa_context_errno(c)));
		lingot_audio_pulseaudio_mainloop_quit(1);
		break;
	}
}

#endif
