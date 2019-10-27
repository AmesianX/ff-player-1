#include <stdio.h>
#include <Windows.h>
#include <ryulib/base.hpp>
#include <ryulib/ThreadQueue.hpp>
#include <SDL2/SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#pragma comment(lib, "sdl2maind.lib")

void audio_callback(void* udata, Uint8* stream, int len)
{
	ThreadQueue<Memory*>* queue = (ThreadQueue<Memory*>*) udata;
	Memory* memory; 
	if (queue->pop(memory)) {
		memcpy(stream, memory->getData(), memory->getSize());
		delete memory;
	}
}

int main(int argc, char* argv[])
{
	string filename = "D:/Work/test.mp4";

	// ����(����� �ҽ�) ����
	AVFormatContext* ctx_format = NULL;
	if (avformat_open_input(&ctx_format, filename.c_str(), NULL, NULL) != 0) return -1;
	if (avformat_find_stream_info(ctx_format, NULL) < 0) return -1;
	
	// ����� ��Ʈ�� ã��
	int audio_stream = -1;
	for (int i = 0; i < ctx_format->nb_streams; i++) {
		if (ctx_format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream = i;
			break;
		}
	}
	if (audio_stream == -1) return -2;

	// ����� �ڵ� ����
	AVCodecParameters* ctx_audio = ctx_format->streams[audio_stream]->codecpar;
	AVCodec* codec = avcodec_find_decoder(ctx_audio->codec_id);
	if (codec == NULL) return -2;
	AVCodecContext* ctx_codec = avcodec_alloc_context3(codec);
	if (avcodec_parameters_to_context(ctx_codec, ctx_audio) != 0)  return -3;
	if (avcodec_open2(ctx_codec, codec, NULL) < 0) return -3;

	ThreadQueue<Memory*> queue;

	// ������� ����� ��ġ ����
	SDL_AudioSpec audio_spec;
	SDL_memset(&audio_spec, 0, sizeof(audio_spec));
	audio_spec.freq = ctx_audio->sample_rate;
	audio_spec.format = AUDIO_F32LSB;
	audio_spec.channels = ctx_audio->channels;
	audio_spec.samples = 1024;
	audio_spec.callback = audio_callback;
	audio_spec.userdata = &queue;
	if(SDL_OpenAudio(&audio_spec, NULL) < 0) {
		printf("SDL_OpenAudio: %s\n", SDL_GetError());
		return -4;
	}

	// ����� ���� ��ȯ (resampling)
	SwrContext* swr = swr_alloc_set_opts(
		NULL,
		ctx_audio->channel_layout,
		AV_SAMPLE_FMT_FLT,
		ctx_audio->sample_rate,
		ctx_audio->channel_layout,
		(AVSampleFormat) ctx_audio->format,
		ctx_audio->sample_rate,
		0,                   
		NULL);
	swr_init(swr);

	printf("channels: %d, sample_rate: %d, %d \n", ctx_audio->channels, ctx_audio->sample_rate, ctx_audio->bit_rate);

	AVFrame* frame = av_frame_alloc();
	if (!frame) return -4;

	AVFrame* reframe = av_frame_alloc();
	if (!reframe) return -4;

	AVPacket packet;

	// ����(����� �ҽ�) �ݺ��ؼ� ������ �б�
	while (av_read_frame(ctx_format, &packet) >= 0) {
		// ����� ��Ʈ���� ó��
		if (packet.stream_index == audio_stream) {
			int ret = avcodec_send_packet(ctx_codec, &packet) < 0;
			if (ret < 0) {
				printf("Error sending a packet for decoding \n");
				return -5;
			}

			while (ret >= 0) {
				ret = avcodec_receive_frame(ctx_codec, frame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				} else if (ret < 0) {
					printf("Error sending a packet for decoding \n");
					return -5;
				}

				// ���� ��ȯ
				reframe->channel_layout = frame->channel_layout;
				reframe->sample_rate = frame->sample_rate;
				reframe->format = AV_SAMPLE_FMT_FLT;
				int ret = swr_convert_frame(swr, reframe, frame);

				// TODO: ������ũ�Ⱑ sample ũ��� ���� ���� ��� ó��
				int data_size = av_samples_get_buffer_size(NULL, ctx_codec->channels, frame->nb_samples, (AVSampleFormat) reframe->format, 0);
				queue.push(new Memory(reframe->data[0], data_size));
				SDL_PauseAudio(0);

				// ������ ó�� �� ������ ��ٸ���
				while (queue.size() > 2) Sleep(1);
			}
		}

		av_packet_unref(&packet);
	}

	return 0;
}