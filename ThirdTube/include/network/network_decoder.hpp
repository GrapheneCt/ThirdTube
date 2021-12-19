#pragma once
#include "network_downloader.hpp"
#include "types.hpp"
#include <vector>
#include <set>
#include <deque>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
}

namespace network_decoder_ {
	/*
		internal queue used to buffer the raw output of decoded images
		thread-safe when one thread only pushes and the other thread pops
	*/
	template <typename T> class output_buffer {
		size_t num = 0;
		std::vector<T> buffer;
		volatile size_t head = 0; // the index of the element in the buffer which the next pushed element should go in
		volatile size_t tail = 0; // the index of the element in the buffer which should be poped next
		
		public :
		void init(const std::vector<T> &buffer_init) {
			num = buffer_init.size() - 1;
			buffer = buffer_init;
			head = tail = 0;
		}
		std::vector<T> deinit() {
			auto res = buffer;
			buffer.clear();
			num = 0;
			head = tail = 0;
			return res;
		}
		// get the size of the queue
		size_t size() {
			if (head >= tail) return head - tail;
			else return head + num + 1 - tail;
		}
		bool full() {
			return size() == num;
		}
		bool empty() {
			return size() == 0;
		}
		T get_next_pushed() {
			if (full()) return NULL;
			return buffer[head];
		}
		bool push() {
			if (full()) return false;
			head = (head == num ? 0 : head + 1);
			return true;
		}
		T get_next_poped() {
			if (empty()) return NULL;
			return buffer[tail];
		}
		bool pop() {
			if (empty()) return false;
			tail = (tail == num ? 0 : tail + 1);
			return true;
		}
		void clear() {
			head = tail;
		}
	};
}

class NetworkDecoder;

class NetworkDecoderFFmpegData {
private :
	static constexpr int VIDEO = 0;
	static constexpr int AUDIO = 1;
	static constexpr int BOTH = 0;
	
	Result_with_string init_(int type, AVMediaType expected_codec_type, NetworkDecoder *parent_decoder);
	AVStream *get_stream(int type) { return format_context[video_audio_seperate ? type : BOTH]->streams[stream_index[type]]; }
public :
	bool video_audio_seperate = false;
	NetworkStream *network_stream[2] = {NULL, NULL};
	std::pair<NetworkDecoder *, NetworkStream *> *opaque[2] = {NULL, NULL};
	AVFormatContext *format_context[2] = {NULL, NULL};
	AVIOContext *io_context[2] = {NULL, NULL};
	int stream_index[2] = {0, 0};
	AVCodecContext *decoder_context[2] = {NULL, NULL};
	SwrContext *swr_context = NULL;
	const AVCodec *codec[2] = {NULL, NULL};
	bool audio_only = false;
	NetworkDecoder *parent_decoder = NULL;
	
	Result_with_string init(NetworkStream *video_stream, NetworkStream *audio_stream, NetworkDecoder *parent_decoder);
	Result_with_string init(NetworkStream *both_stream, NetworkDecoder *parent_decoder);
	void deinit(bool deinit_stream);
	Result_with_string reinit();
	double get_duration();
};

class NetworkDecoder {
private :
	static constexpr int VIDEO = 0;
	static constexpr int AUDIO = 1;
	static constexpr int BOTH = 0;
	
	// ffmpeg data
	bool video_audio_seperate = false;
	NetworkStream *network_stream[2] = {NULL, NULL};
	std::pair<NetworkDecoder *, NetworkStream *> *opaque[2] = {NULL, NULL};
	AVFormatContext *format_context[2] = {NULL, NULL};
	AVIOContext *io_context[2] = {NULL, NULL};
	int stream_index[2] = {0, 0};
	AVCodecContext *decoder_context[2] = {NULL, NULL};
	SwrContext *swr_context = NULL;
	const AVCodec *codec[2] = {NULL, NULL};
	bool audio_only = false;
	
	std::deque<AVPacket *> packet_buffer[2];
	network_decoder_::output_buffer<AVFrame *> video_tmp_frames;
	network_decoder_::output_buffer<u8 *> video_mvd_tmp_frames;
	u8 *mvd_frame = NULL; // internal buffer written directly by the mvd service
	u8 *sw_video_output_tmp = NULL;
	Handle buffered_pts_list_lock; // lock of buffered_pts_list
	std::multiset<double> buffered_pts_list; // used for HW decoder to determine the pts when outputting a frame
	bool mvd_first = false;
	
	Result_with_string init_output_buffer(bool);
	Result_with_string read_packet(int type);
	Result_with_string mvd_decode(int *width, int *height);
	AVStream *get_stream(int type) { return format_context[video_audio_seperate ? type : BOTH]->streams[stream_index[type]]; }
public :
	bool hw_decoder_enabled = false;
	volatile bool interrupt = false;
	volatile bool need_reinit = false;
	volatile bool ready = false;
	double timestamp_offset = 0;
	const char *get_network_waiting_status() {
		if (network_stream[VIDEO] && network_stream[VIDEO]->network_waiting_status) return network_stream[VIDEO]->network_waiting_status;
		if (network_stream[AUDIO] && network_stream[AUDIO]->network_waiting_status) return network_stream[AUDIO]->network_waiting_status;
		return NULL;
	}
	std::vector<std::pair<double, std::vector<double> > > get_buffering_progress_bars(int bar_len);
	
	Result_with_string change_ffmpeg_data(const NetworkDecoderFFmpegData &data, double timestamp_offset);
	Result_with_string init(bool request_hw_decoder); // should be called after the call of change_ffmpeg_data()
	void deinit();
	void clear_buffer();
	
	struct VideoFormatInfo {
		int width;
		int height;
		double framerate;
		std::string format_name;
		double duration;
	};
	VideoFormatInfo get_video_info();
	
	struct AudioFormatInfo {
		int bitrate;
		int sample_rate;
		int ch;
		std::string format_name;
		double duration;
	};
	AudioFormatInfo get_audio_info();
	
	enum class DecodeType {
		AUDIO,
		VIDEO,
		EoF, // EOF is reserved so...
		INTERRUPTED
	};
	DecodeType next_decode_type();
	
	
	// decode the previously read video packet
	// decoded image is stored internally and can be acquired via get_decoded_video_frame()
	Result_with_string decode_video(int *width, int *height, bool *key_frame, double *cur_pos);
	
	// decode the previously read audio packet
	Result_with_string decode_audio(int *size, u8 **data, double *cur_pos);
	
	// get the previously decoded video frame raw data
	// the pointer stored in *data should NOT be freed
	Result_with_string get_decoded_video_frame(int width, int height, u8** data, double *cur_pos);
	
	// seek both audio and video
	Result_with_string seek(s64 microseconds);
};

