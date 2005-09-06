/*
 * vISDN
 *
 * Copyright (C) 2004-2005 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/types.h>

#include <alsa/asoundlib.h>

struct sniffer_state
{
	char *pcm_name;

	snd_pcm_t *pcm;
	snd_pcm_stream_t stream;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_format_t format;

	int rate;
	int exact_rate;

	int dir;
	int periods;
	snd_pcm_sframes_t period_size;
	unsigned int buffer_time;
	unsigned int period_time;
};

static int xrun_recovery(snd_pcm_t *handle, int err)
{
	if (err == -EPIPE) {    /* under-run */
		err = snd_pcm_prepare(handle);
		if (err < 0)
			printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
		return 0;
	} else if (err == -ESTRPIPE) {
		while ((err = snd_pcm_resume(handle)) == -EAGAIN)
			sleep(1);       /* wait until the suspend flag is released */
		if (err < 0) {
			err = snd_pcm_prepare(handle);
			if (err < 0)
				printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
		}
		return 0;
	}
	return err;
}

static void async_direct_callback(snd_async_handler_t *ahandler)
{
	snd_pcm_t *handle = snd_async_handler_get_pcm(ahandler);
	struct sniffer_state *sns = snd_async_handler_get_callback_private(ahandler);

	const snd_pcm_channel_area_t *my_areas;
	snd_pcm_uframes_t offset, frames, size;
	snd_pcm_sframes_t avail, commitres;
	snd_pcm_state_t state;
	int first = 0, err;

	while (1) {
		state = snd_pcm_state(handle);
		if (state == SND_PCM_STATE_XRUN) {
			err = xrun_recovery(handle, -EPIPE);
			if (err < 0) {
				printf("XRUN recovery failed: %s\n", snd_strerror(err));
				exit(EXIT_FAILURE);
			}

			first = 1;

		} else if (state == SND_PCM_STATE_SUSPENDED) {

			err = xrun_recovery(handle, -ESTRPIPE);

			if (err < 0) {
				printf("SUSPEND recovery failed: %s\n", snd_strerror(err));
				exit(EXIT_FAILURE);
			}
		}

		avail = snd_pcm_avail_update(handle);

		if (avail < 0) {
			err = xrun_recovery(handle, avail);
			if (err < 0) {
				printf("avail update failed: %s\n", snd_strerror(err));
				exit(EXIT_FAILURE);
			}
			first = 1;
			continue;
		}

		if (avail < sns->period_size) {
			if (first) {

				first = 0;

				err = snd_pcm_start(handle);
				if (err < 0) {
					printf("Start error: %s\n", snd_strerror(err));
					exit(EXIT_FAILURE);
				}
			} else {
				break;
			}

			continue;
		}

		size = sns->period_size;
		while (size > 0) {
			frames = size;
			err = snd_pcm_mmap_begin(handle, &my_areas, &offset, &frames);
			if (err < 0) {
				if ((err = xrun_recovery(handle, err)) < 0) {
					printf("MMAP begin avail error: %s\n", snd_strerror(err));
					exit(EXIT_FAILURE);
				}
				first = 1;
			}

	printf("Callback %d %d\n", offset, frames);

			int i;
			for(i=0; i<frames; i++)
				*(__u8 *)(my_areas[0].addr + offset + i)=i%64;

			//generate_sine(my_areas, offset, frames, &sns->phase);

			commitres = snd_pcm_mmap_commit(handle, offset, frames);
			if (commitres < 0 || (snd_pcm_uframes_t)commitres != frames) {
				if ((err = xrun_recovery(handle, commitres >= 0 ? -EPIPE : commitres)) < 0) {
					printf("MMAP commit error: %s\n", snd_strerror(err));
					exit(EXIT_FAILURE);
				}
				first = 1;
			}
			size -= frames;
		}
	}
}

int main()
{
	int err;
	struct sniffer_state sts;

	sts.pcm_name = strdup("plughw:0,0");
	sts.stream = SND_PCM_STREAM_PLAYBACK;
	sts.format = SND_PCM_FORMAT_A_LAW;
	sts.rate = 8000;
	sts.exact_rate;
	sts.periods = 2;
	sts.buffer_time = 25000;
	sts.period_time = 12500;

	snd_pcm_hw_params_alloca(&sts.hwparams);

	if (snd_pcm_open(&sts.pcm, sts.pcm_name, sts.stream, 0) < 0) {
		fprintf(stderr, "Error opening PCM device %s\n", sts.pcm_name);
		return(-1);
	}

	if (snd_pcm_hw_params_any(sts.pcm, sts.hwparams) < 0) {
		fprintf(stderr, "Can not configure this PCM device.\n");
		return(-1);
	}

	if (snd_pcm_hw_params_set_access(sts.pcm, sts.hwparams,
			SND_PCM_ACCESS_MMAP_NONINTERLEAVED) < 0) {
		fprintf(stderr, "Error setting access.\n");
		return(-1);
	}

	if (snd_pcm_hw_params_set_format(sts.pcm, sts.hwparams,
			sts.format) < 0) {
		fprintf(stderr, "Error setting format.\n");
		return(-1);
	}

	sts.exact_rate = sts.rate;
	if (snd_pcm_hw_params_set_rate_near(sts.pcm, sts.hwparams,
			&sts.exact_rate, 0) < 0) {
		fprintf(stderr, "Error setting rate.\n");
		return(-1);
	}

printf("rate: %d\n", sts.exact_rate);

	if (sts.rate != sts.exact_rate) {
		fprintf(stderr, "The rate %d Hz is not supported by your hardware.\n"
			"==> Using %d Hz instead.\n", sts.rate, sts.exact_rate);
	}

	if (snd_pcm_hw_params_set_channels(sts.pcm, sts.hwparams, 1) < 0) {
		fprintf(stderr, "Error setting channels.\n");
		return(-1);
	}

	if (snd_pcm_hw_params_set_periods(sts.pcm, sts.hwparams, sts.periods, 0) < 0) {
		fprintf(stderr, "Error setting periods.\n");
		return(-1);
	}

	if (snd_pcm_hw_params_set_buffer_time_near(sts.pcm, sts.hwparams,
			&sts.buffer_time, &sts.dir) < 0) {
		fprintf(stderr, "Error setting buffersize.\n");
		return(-1);
	}

printf("buffer_time set to %d\n", sts.buffer_time);

	err = snd_pcm_hw_params_get_period_size(sts.hwparams, &sts.period_size, &sts.dir);
	if (err < 0) {
		printf("Unable to get period size for playback: %s\n", snd_strerror(err));
		return err;
	}

printf("period_size = %d\n", sts.period_size);

	if (snd_pcm_hw_params(sts.pcm, sts.hwparams) < 0) {
		fprintf(stderr, "Error setting HW params.\n");
		return(-1);
	}



sleep(2);



	setvbuf(stdout, (char *)NULL, _IONBF, 0);

	int f;
	f = open("/dev/visdn/streamport/0", O_RDONLY);
	if (f < 0) {
		printf("open: %s\n", strerror(errno));
	}


	double phase = 0;
	const snd_pcm_channel_area_t *my_areas;
	snd_pcm_uframes_t offset, frames, size;
	snd_pcm_sframes_t avail, commitres;
	snd_pcm_state_t state;
	int first = 1;

	while (1) {
		state = snd_pcm_state(sts.pcm);

		if (state == SND_PCM_STATE_XRUN) {
			err = xrun_recovery(sts.pcm, -EPIPE);
			if (err < 0) {
				printf("XRUN recovery failed: %s\n", snd_strerror(err));
				return err;
			}
			first = 1;
		} else if (state == SND_PCM_STATE_SUSPENDED) {
			err = xrun_recovery(sts.pcm, -ESTRPIPE);
			if (err < 0) {
				printf("SUSPEND recovery failed: %s\n", snd_strerror(err));
				return err;
			}
		}

		avail = snd_pcm_avail_update(sts.pcm);
		if (avail < 0) {
			err = xrun_recovery(sts.pcm, avail);
			if (err < 0) {
				printf("avail update failed: %s\n", snd_strerror(err));
				return err;
			}
			first = 1;
			continue;
		}

		if (avail < sts.period_size) {
			if (first) {
				first = 0;
				err = snd_pcm_start(sts.pcm);
				if (err < 0) {
					printf("Start error: %s\n", snd_strerror(err));
					exit(EXIT_FAILURE);
				}
			} else {
				err = snd_pcm_wait(sts.pcm, -1);
				if (err < 0) {
					if ((err = xrun_recovery(sts.pcm, err)) < 0) {
						printf("snd_pcm_wait error: %s\n", snd_strerror(err));
						exit(EXIT_FAILURE);
					}
					first = 1;
				}
			}
			continue;
		}

		size = sts.period_size;
		while (size > 0) {
			frames = size;
			err = snd_pcm_mmap_begin(sts.pcm, &my_areas, &offset, &frames);
			if (err < 0) {
				if ((err = xrun_recovery(sts.pcm, err)) < 0) {
					printf("MMAP begin avail error: %s\n", snd_strerror(err));
					exit(EXIT_FAILURE);
				}

				first = 1;
			}

			int r = read(f, my_areas[0].addr + offset, frames);
			printf("%d %d %d: ", offset, frames, r);

			int i;
			for (i=0; i<r; i++)
				printf("%02x", *(__u8 *)(my_areas[0].addr + i));

			printf("\n");

			commitres = snd_pcm_mmap_commit(sts.pcm, offset, frames);
			if (commitres < 0 || (snd_pcm_uframes_t)commitres != frames) {
				if ((err = xrun_recovery(sts.pcm, commitres >= 0 ? -EPIPE : commitres)) < 0) {
					printf("MMAP commit error: %s\n", snd_strerror(err));
					exit(EXIT_FAILURE);
				}
				first = 1;
			}
			size -= frames;
		}
	}

	return 0;
}
