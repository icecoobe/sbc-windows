/*
 *
 *  Bluetooth low-complexity, subband codec (SBC) decoder
 *
 *  Copyright (C) 2008-2010  Nokia Corporation
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *  Copyright (C) 2012-2013  Intel Corporation
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../sbc/sbc.h"
#include "../formats.h"


#define BUF_SIZE 8192

#define APP_WAVE_HDR_SIZE 44
const unsigned char app_wav_hdr[APP_WAVE_HDR_SIZE] =
{
    'R', 'I', 'F', 'F',         /* Chunk ID : "RIFF" */
    '\0', '\0', '\0', '\0',     /* Chunk size = file size - 8 */
    'W', 'A', 'V', 'E',         /* Chunk format : "WAVE" */
    'f', 'm', 't', ' ',         /*   Subchunk ID : "fmt " */
    0x10, 0x00, 0x00, 0x00,     /*   Subchunk size : 16 for PCM format */
    0x01, 0x00,                 /*     Audio format : 1 means PCM linear */
    '\0', '\0',                 /*     Number of channels */
    '\0', '\0', '\0', '\0',     /*     Sample rate */
    '\0', '\0', '\0', '\0',     /*     Byte rate = SampleRate * NumChannels * BitsPerSample/8 */
    '\0', '\0',                 /*     Blockalign = NumChannels * BitsPerSample/8 */
    '\0', '\0',                 /*     Bitpersample */
    'd', 'a', 't', 'a',         /*   Subchunk ID : "data" */
    '\0', '\0', '\0', '\0'      /*   Subchunk size = NumSamples * NumChannels * BitsPerSample/8 */
};

/*
 * convert_snd_to_wav
 *
 * filename: snd file path
 * outfile: pcm file path
 */
static void convert_snd_to_wav(const char *filename, const char *wavfile)
{
	unsigned char *stream;
	struct stat st;
	
	if (stat(filename, &st) < 0) {
		fprintf(stderr, "Can't get size of file %s: %s\n",
						filename, strerror(errno));
		return;
	}

	stream = (unsigned char*)malloc(st.st_size);
	if (!stream) {
		fprintf(stderr, "Can't allocate memory for %s: %s\n",
						filename, strerror(errno));
		return;
	}
	FILE *fp = fopen(filename, "rb");
	int r = fread(stream, st.st_size, 1, fp);
	if ((r * st.st_size) != st.st_size) {
		fprintf(stderr, "Can't read content of %s: %s (%d, %d)\n",
			filename, strerror(errno), r, st.st_size);
		fclose(fp);
		goto free;
	}
	fclose(fp);
	
	const int snd_header_size = 24;
	unsigned char *snd_data = (stream + snd_header_size);
	unsigned char *pcm = (unsigned char *)malloc(st.st_size - snd_header_size);
	int data_size = st.st_size - snd_header_size;
	for (int i = 0; i < data_size; i = i + 2)
	{
		*(pcm + i) = *(snd_data + i + 1);
		*(pcm + i + 1) = *(snd_data + i);
	}
	unsigned char   nb_channels;
    //unsigned char   stereo_mode;
    unsigned long   sample_rate;
    unsigned short  bits_per_sample;
	nb_channels = 1;
    sample_rate = 16000;
    bits_per_sample = 16;

	int byte_rate;
	int block_align;
	if (bits_per_sample == 8)
    {
        byte_rate = nb_channels * sizeof(char) * sample_rate;
        block_align = nb_channels * sizeof(char);
    }
    else
    {
        byte_rate = nb_channels * sizeof(short) * sample_rate;
        block_align = nb_channels * sizeof(short);
    }
	int chunk_size = 0;
	chunk_size = data_size + 36;

	unsigned char header[APP_WAVE_HDR_SIZE];
	/* Copy the standard header */
    memcpy(header, app_wav_hdr, sizeof(header));
	// Update wav header section
	header[4] = (unsigned char)chunk_size;
    header[5] = (unsigned char)(chunk_size >> 8);
    header[6] = (unsigned char)(chunk_size >> 16);
    header[7] = (unsigned char)(chunk_size >> 24);

    header[22] = (unsigned char)nb_channels;
    header[23] = 0; /* nb_channels is coded on 1 byte only */

    header[24] = (unsigned char)sample_rate;
    header[25] = (unsigned char)(sample_rate >> 8);
    header[26] = (unsigned char)(sample_rate >> 16);
    header[27] = (unsigned char)(sample_rate >> 24);

    header[28] = (unsigned char)byte_rate;
    header[29] = (unsigned char)(byte_rate >> 8);
    header[30] = (unsigned char)(byte_rate >> 16);
    header[31] = (unsigned char)(byte_rate >> 24);

    header[32] = (unsigned char)block_align;
    header[33] = (unsigned char)(block_align >> 8);

    header[34] = (unsigned char)bits_per_sample;
    header[35] = (unsigned char)(bits_per_sample >> 8);

    header[40] = (unsigned char)data_size;
    header[41] = (unsigned char)(data_size >> 8);
    header[42] = (unsigned char)(data_size >> 16);
    header[43] = (unsigned char)(data_size >> 24);

	fp = fopen(wavfile, "wb+");
	fwrite(header, 1, APP_WAVE_HDR_SIZE, fp); 
	fwrite(pcm, data_size, 1, fp);
	printf("data_size = %ld\n", data_size);
close:
	fclose(fp);

free:
	free(stream);
	free(pcm);
}

/*
 * sbc_to_snd
 *
 * filename: sbc.bin file, it's a binary file of sbc raw data
 * outfile: snd file path, save the decoded snd audio stream.
 * msbc: whether the sbc file is encoded in msbc mode
 */
static void sbc_to_snd(char *filename, char *output, int msbc)
{
	unsigned char buf[BUF_SIZE], *stream;
	struct stat st;
	sbc_t sbc;
	int pos, streamlen, framelen, count, channels;
	size_t len;
	//int format = AFMT_S16_BE, frequency, channels;
	uint16_t frequency;
	ssize_t written;

	if (stat(filename, &st) < 0) {
		fprintf(stderr, "Can't get size of file %s: %s\n",
						filename, strerror(errno));
		return;
	}

	stream = (unsigned char*)malloc(st.st_size);
	if (!stream) {
		fprintf(stderr, "Can't allocate memory for %s: %s\n",
						filename, strerror(errno));
		return;
	}
	FILE *fp = fopen(filename, "rb");
	int r = fread(stream, st.st_size, 1, fp);
	if ((r * st.st_size) != st.st_size) {
		fprintf(stderr, "Can't read content of %s: %s (%d, %d)\n",
			filename, strerror(errno), r, st.st_size);
		fclose(fp);
		goto free;
	}
	fclose(fp);

	pos = 0;
	streamlen = st.st_size;

	fp = fopen(output, "wb+");

	if (fp < 0) {
		fprintf(stderr, "Can't open output %s: %s\n",
						output, strerror(errno));
		goto free;
	}

	if (msbc)
		sbc_init_msbc(&sbc, 0L);
	else
		sbc_init(&sbc, 0L);
	sbc.endian = SBC_BE;

	framelen = sbc_decode(&sbc, stream, streamlen, buf, sizeof(buf), &len);
	channels = sbc.mode == SBC_MODE_MONO ? 1 : 2;
	switch (sbc.frequency) {
	case SBC_FREQ_16000:
		frequency = 16000;
		break;

	case SBC_FREQ_32000:
		frequency = 32000;
		break;

	case SBC_FREQ_44100:
		frequency = 44100;
		break;

	case SBC_FREQ_48000:
		frequency = 48000;
		break;
	default:
		frequency = 0;
	}

	fprintf(stderr,"decoding %s with rate %d, %d subbands, "
		"%d bits, allocation method %s and mode %s\n",
		filename, frequency, sbc.subbands * 4 + 4, sbc.bitpool,
		sbc.allocation == SBC_AM_SNR ? "SNR" : "LOUDNESS",
		sbc.mode == SBC_MODE_MONO ? "MONO" :
				sbc.mode == SBC_MODE_STEREO ?
					"STEREO" : "JOINTSTEREO");

	struct au_header au_hdr;

	au_hdr.magic       = AU_MAGIC;
	au_hdr.hdr_size    = BE_INT(24);
	au_hdr.data_size   = BE_INT(0);
	au_hdr.encoding    = BE_INT(AU_FMT_LIN16);
	au_hdr.sample_rate = BE_INT(frequency);
	au_hdr.channels    = BE_INT(channels);

	written = fwrite(&au_hdr, sizeof(au_hdr), 1, fp);
	written *= sizeof(au_hdr);
	// written = write(ad, &au_hdr, sizeof(au_hdr));
	if (written < (ssize_t) sizeof(au_hdr)) {
		fprintf(stderr, "Failed to write header\n");
		goto close;
	}

	count = len;

	while (framelen > 0) {
		/* we have completed an sbc_decode at this point sbc.len is the
		 * length of the frame we just decoded count is the number of
		 * decoded bytes yet to be written */

		if (count + len >= BUF_SIZE) {
			/* buffer is too full to stuff decoded audio in so it
			 * must be written to the device */
			written = fwrite(buf, 1, count, fp);
			written *= 1;
			// written = write(ad, buf, count);
			if (written > 0)
				count -= written;
		}

		/* sanity check */
		if (count + len >= BUF_SIZE) {
			fprintf(stderr,
				"buffer size of %d is too small for decoded"
				" data (%lu)\n", BUF_SIZE, (unsigned long) (len + count));
			exit(1);
		}

		/* push the pointer in the file forward to the next bit to be
		 * decoded tell the decoder to decode up to the remaining
		 * length of the file (!) */
		pos += framelen;
		framelen = sbc_decode(&sbc, stream + pos, streamlen - pos,
					buf + count, sizeof(buf) - count, &len);

		/* increase the count */
		count += len;
	}

	if (count > 0) {
		written = fwrite(buf, 1, count, fp);
		written *= 1;
		// written = write(ad, buf, count);
		if (written > 0)
			count -= written;
	}

close:
	sbc_finish(&sbc);

	fclose (fp);
free:
	free(stream);
}

/*
 * sbc_to_pcm
 *
 * filename: sbc.bin file, it's a binary file of sbc raw data
 * outfile: wav file path, save the decoded pcm audio stream.
 * msbc: whether the sbc file is encoded in msbc mode
 */
static void sbc_to_pcm(char *filename, char *output, int msbc)
{
	unsigned char buf[BUF_SIZE], *stream;
	struct stat st;
	sbc_t sbc;
	int pos, streamlen, framelen, count, channels;
	size_t len;

	uint16_t frequency;
	ssize_t written;

	if (stat(filename, &st) < 0) {
		fprintf(stderr, "Can't get size of file %s: %s\n",
						filename, strerror(errno));
		return;
	}

	stream = (unsigned char*)malloc(st.st_size);
	if (!stream) {
		fprintf(stderr, "Can't allocate memory for %s: %s\n",
						filename, strerror(errno));
		return;
	}
	FILE *fp = fopen(filename, "rb");
	int r = fread(stream, st.st_size, 1, fp);
	if ((r * st.st_size) != st.st_size) {
		fprintf(stderr, "Can't read content of %s: %s (%d, %d)\n",
			filename, strerror(errno), r, st.st_size);
		fclose(fp);
		goto free;
	}
	fclose(fp);

	pos = 0;
	streamlen = st.st_size;

	fp = fopen(output, "wb+");

	if (fp < 0) {
		fprintf(stderr, "Can't open output %s: %s\n",
						output, strerror(errno));
		goto free;
	}

	if (msbc)
		sbc_init_msbc(&sbc, 0L);
	else
		sbc_init(&sbc, 0L);
	sbc.endian = SBC_LE;

// for processing sbc.bin
// each sbc frame is 57 bytes ===> 120 * 2 = 240 bytes pcm frame
#define APP_HH_NBYTES_PER_FRAME             57	// each sbc frame size
#define APP_HH_NSAMPLES_PER_FRAME_MSBC      120 // each decoded frame size (must * 2)

	int sbc_frame_count = st.st_size / 57;
	unsigned char *pcm = (unsigned char *)malloc(sbc_frame_count * 240);
	printf("sbc_frame_count=%ld\n", sbc_frame_count);

	
	unsigned char   nb_channels;
    //unsigned char   stereo_mode;
    unsigned long   sample_rate;
    unsigned short  bits_per_sample;
	nb_channels = 1;
    sample_rate = 16000;
    bits_per_sample = 16;

	int byte_rate;
	int block_align;
	if (bits_per_sample == 8)
    {
        byte_rate = nb_channels * sizeof(char) * sample_rate;
        block_align = nb_channels * sizeof(char);
    }
    else
    {
        byte_rate = nb_channels * sizeof(short) * sample_rate;
        block_align = nb_channels * sizeof(short);
    }

	int data_size = 0;
	for (int i = 0; i < sbc_frame_count; i++)
	{
		framelen = sbc_decode(&sbc, stream + i * 57, 57, pcm + i * 240, 240, &len);
		data_size += 240;
	}
	int chunk_size = data_size + 36;

	unsigned char header[APP_WAVE_HDR_SIZE];
	/* Copy the standard header */
    memcpy(header, app_wav_hdr, sizeof(header));
	// Update wav header section
	header[4] = (unsigned char)chunk_size;
    header[5] = (unsigned char)(chunk_size >> 8);
    header[6] = (unsigned char)(chunk_size >> 16);
    header[7] = (unsigned char)(chunk_size >> 24);

    header[22] = (unsigned char)nb_channels;
    header[23] = 0; /* nb_channels is coded on 1 byte only */

    header[24] = (unsigned char)sample_rate;
    header[25] = (unsigned char)(sample_rate >> 8);
    header[26] = (unsigned char)(sample_rate >> 16);
    header[27] = (unsigned char)(sample_rate >> 24);

    header[28] = (unsigned char)byte_rate;
    header[29] = (unsigned char)(byte_rate >> 8);
    header[30] = (unsigned char)(byte_rate >> 16);
    header[31] = (unsigned char)(byte_rate >> 24);

    header[32] = (unsigned char)block_align;
    header[33] = (unsigned char)(block_align >> 8);

    header[34] = (unsigned char)bits_per_sample;
    header[35] = (unsigned char)(bits_per_sample >> 8);

    header[40] = (unsigned char)data_size;
    header[41] = (unsigned char)(data_size >> 8);
    header[42] = (unsigned char)(data_size >> 16);
    header[43] = (unsigned char)(data_size >> 24);

	fwrite(header, 1, APP_WAVE_HDR_SIZE, fp); 
	fwrite(pcm, data_size, 1, fp);
	printf("data_size = %ld\n", data_size);

close:
	sbc_finish(&sbc);
	fclose (fp);

free:
	free(stream);
	free(pcm);
}

int main(int argc, char *argv[])
{
	sbc_to_pcm("sbc.bin", "sbc_to_pcm.wav", 1);
	sbc_to_snd("sbc.bin", "sbc_to_snd.snd", 1);
	convert_snd_to_wav("sbc_to_snd.snd", "snd_to_pcm.wav");

	return 0;
}
