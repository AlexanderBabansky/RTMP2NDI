#include <iostream>
#include <list>
#include <unordered_set>
#include <cassert>
#include <fstream>
#include <mutex>
#include <chrono>
#include <thread>
#include <memory>
#include "cxxopts.hpp"
#include "Processing.NDI.Lib.h"
#include "easyrtmp/data_layers/tcp_network.h"
#include "easyrtmp/data_layers/openssl_tls.h"
#include "easyrtmp/rtmp_server_session.h"
#include "perfmon.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
}

#define MAX_AUDIO_CHANNELS 6
#define NDI_AUDIO_FRAMESIZE 1024

using namespace std;

bool use_tls = false;
string cert_path, key_path;
uint16_t port = 1935;
bool use_hw = false;
string allow_keys_path;
unordered_set<string> allowed_keys;
uint16_t delay = 0;
uint16_t drop_buffer = 200;
bool use_multithreading_cpu = true;
bool ignore_timestamps = false;
bool print_fps = false;

struct AudioVideoFrame {
    uint64_t timestamp = 0;
    AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    bool is_keyframe = false;
    uint32_t composition_time = 0;
    DATA_BYTES data;
};

template<typename T>
void set_optional_parameter(cxxopts::ParseResult& parsed, T& out, const char* par_name) {
    try {
        out = parsed[par_name].as<T>();
    }
    catch (...) {}
}

AVCodecID RTMPVideoCodecToFFMpeg(librtmp::RTMPVideoCodec vid_c) {
    switch (vid_c)
    {
    case librtmp::RTMPVideoCodec::AVC:
        return AV_CODEC_ID_H264;
    case librtmp::RTMPVideoCodec::HEVC:
        return AV_CODEC_ID_HEVC;
    default:
        return AV_CODEC_ID_NONE;
    }
}

AVCodecID RTMPAudioCodecToFFMpeg(librtmp::RTMPAudioCodec aud_c) {
    switch (aud_c)
    {
    case librtmp::RTMPAudioCodec::AAC:
        return AV_CODEC_ID_AAC;
    case librtmp::RTMPAudioCodec::OPUS:
        return AV_CODEC_ID_OPUS;
    default:
        return AV_CODEC_ID_NONE;
    }
}

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
    const enum AVPixelFormat* pix_fmts)
{
    const enum AVPixelFormat* p;

    for (p = pix_fmts; *p != -1; p++) {
#ifdef WIN32
        if (*p == AV_PIX_FMT_D3D11)
#endif
            return *p;
    }

    fprintf(stderr, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

int av_image_get_plane_size(enum AVPixelFormat pix_fmt,
    int width, int height, int align, int plane_id)
{
    assert(plane_id < 4);
    int ret, i;
    int linesize[4];
    ptrdiff_t aligned_linesize[4];
    size_t sizes[4];
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    ret = av_image_check_size(width, height, 0, NULL);
    if (ret < 0)
        return ret;

    ret = av_image_fill_linesizes(linesize, pix_fmt, width);
    if (ret < 0)
        return ret;

    for (i = 0; i < 4; i++)
        aligned_linesize[i] = FFALIGN(linesize[i], align);

    ret = av_image_fill_plane_sizes(sizes, pix_fmt, height, aligned_linesize);
    if (ret < 0)
        return ret;

    return sizes[plane_id];
}

int hw_decoder_ctx_init(AVCodecContext* ctx, const enum AVHWDeviceType type, AVBufferRef** hw_device_ctx)
{
    assert(ctx && hw_device_ctx);
    int err = 0;
    if ((err = av_hwdevice_ctx_create(hw_device_ctx, type,
        NULL, NULL, 0)) < 0) {
        return err;
    }
    ctx->hw_device_ctx = av_buffer_ref(*hw_device_ctx);
    return err;
}

NDIlib_FourCC_video_type_e FFMpegPixelFormatToNDI(AVPixelFormat format) {
    switch (format)
    {
    case AV_PIX_FMT_YUV420P:
        return NDIlib_FourCC_type_I420;
    case AV_PIX_FMT_NV12:
        return NDIlib_FourCC_type_NV12;
    }
    return NDIlib_FourCC_video_type_max;
}

struct DecoderStruct {
    mutex buffers_mutex;
    multimap<uint64_t, AudioVideoFrame> media_buffer;
    recursive_mutex decoder_mutex;
    bool decoding = true;
    thread decoding_thread;
    chrono::high_resolution_clock::time_point start_time;
    uint64_t decoding_offset = UINT64_MAX;
    bool reset_decoding_offset = true;

    AVCodecContext* video_ctx = nullptr, * audio_ctx = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;
    AVPixelFormat sw_pix_fmt = AV_PIX_FMT_NONE;
    struct SwsContext* sws_ctx = nullptr;
    struct SwrContext* swr_ctx = nullptr;
    uint8_t* dst_data[4]{ nullptr };
    int dst_linesize[4]{ 0 };
    uint8_t** dst_audio_data = nullptr;
    int dst_audio_linesize;
    AVPacket* vid_pkt = nullptr, * aud_pkt = nullptr;
    AVFrame* vid_frame = nullptr, * sw_frame = nullptr, * aud_frame = nullptr;
    Perfmon perfmon;

    NDIlib_video_frame_v2_t ndi_video_frame{ 0 };
    NDIlib_audio_frame_interleaved_16s_t  ndi_audio_frame{ 0 };
    NDIlib_send_instance_t ndi_sender = nullptr;

    DecoderStruct(string key) {
        chrono::high_resolution_clock::time_point sss;
        assert(key.size());
        vid_pkt = av_packet_alloc();
        aud_pkt = av_packet_alloc();
        vid_frame = av_frame_alloc();
        aud_frame = av_frame_alloc();
        auto sender_config = NDIlib_send_create_t(key.c_str(), 0, false, false);
        ndi_sender = NDIlib_send_create(&sender_config);
        if (!ndi_sender) {
            cout << "Could not create NDI instance" << endl;
            throw 0;
        }
        ndi_video_frame.FourCC = NDIlib_FourCC_type_BGRA;
        ndi_video_frame.frame_format_type = NDIlib_frame_format_type_progressive;
        ndi_video_frame.p_data = nullptr;
        decoding_thread = thread(&DecoderStruct::DecodingThreadVoid, this);
    }

    ~DecoderStruct() {
        CleanupVideo();
        CleanupAudio();
        av_frame_free(&vid_frame);
        av_frame_free(&aud_frame);
        av_packet_free(&vid_pkt);
        av_packet_free(&aud_pkt);
        NDIlib_send_destroy(ndi_sender);
        decoding = false;
        if (decoding_thread.joinable())
            decoding_thread.join();
    }

    DecoderStruct(const DecoderStruct&) = delete;
    DecoderStruct(DecoderStruct&&) = delete;

    bool DecodeVideoAndSendNdi(bool key_frame, uint64_t timestamp, uint32_t composition_time, DATA_BYTES data) {
        if (key_frame)
            vid_pkt->flags = AV_PKT_FLAG_KEY;
        else
            vid_pkt->flags = 0;
        vid_pkt->dts = timestamp;
        vid_pkt->pts = timestamp + composition_time;
        vid_pkt->data = (uint8_t*)data.data();
        vid_pkt->size = data.size();

        {
            lock_guard<recursive_mutex> g(decoder_mutex);
            assert(video_ctx);
            int ret = avcodec_send_packet(video_ctx, vid_pkt);
            if (ret < 0) {
                cout << "Error sending a video packet for decoding" << endl;
                return false;
            }
            ret = avcodec_receive_frame(video_ctx, vid_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return true;
            if (ret < 0) {
                cout << "Error during video decoding" << endl;
                return false;
            }
            AVFrame* tmp_frame = nullptr;
            if (vid_frame->format == hw_pix_fmt) {
                if (av_hwframe_transfer_data(sw_frame, vid_frame, 0) < 0) {
                    cout << "Error transferring the data from GPU to CPU" << endl;
                    return false;
                }
                tmp_frame = sw_frame;
            }
            else
                tmp_frame = vid_frame;

            NDIlib_send_send_video_async_v2(ndi_sender, nullptr);
            if (!ndi_video_frame.p_data || sw_pix_fmt != (AVPixelFormat)tmp_frame->format) {
                sw_pix_fmt = (AVPixelFormat)tmp_frame->format;
                auto frame_size = av_image_get_buffer_size(sw_pix_fmt, video_ctx->width, video_ctx->height, 1);
                ndi_video_frame.p_data = new uint8_t[frame_size];
            }

            ndi_video_frame.xres = tmp_frame->width;
            ndi_video_frame.yres = tmp_frame->height;
            ndi_video_frame.timecode = vid_frame->pts * 1000000;
            ndi_video_frame.FourCC = FFMpegPixelFormatToNDI((AVPixelFormat)tmp_frame->format);;
            ndi_video_frame.line_stride_in_bytes = tmp_frame->linesize[0];
            {
                int offset = 0;
                int size = 0;
                for (auto i = 0; i < av_pix_fmt_count_planes((AVPixelFormat)tmp_frame->format); i++) {
                    size = av_image_get_plane_size((AVPixelFormat)tmp_frame->format, video_ctx->width, video_ctx->height, 1, i);
                    memcpy(ndi_video_frame.p_data + offset, tmp_frame->data[i], size);
                    offset += size;
                }
            }
            NDIlib_send_send_video_async_v2(ndi_sender, &ndi_video_frame);
        }

        return true;
    }

    bool DecodeAudioAndSendNdi(uint64_t timestamp, DATA_BYTES data) {
        aud_pkt->pts = timestamp;
        aud_pkt->data = (uint8_t*)data.data();
        aud_pkt->size = data.size();
        {
            lock_guard<recursive_mutex> g(decoder_mutex);
            int ret = avcodec_send_packet(audio_ctx, aud_pkt);
            if (ret < 0) {
                cout << "Error sending audio packet for decoding" << endl;
                return false;
            }
            ret = avcodec_receive_frame(audio_ctx, aud_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return true;
            if (ret < 0) {
                cout << "Error during audio decoding" << endl;
                return false;
            }
            if (!swr_ctx) {
                swr_ctx = swr_alloc();
                av_opt_set_int(swr_ctx, "in_channel_layout", audio_ctx->channel_layout, 0);
                av_opt_set_int(swr_ctx, "in_sample_rate", audio_ctx->sample_rate, 0);
                av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", (AVSampleFormat)aud_frame->format, 0);
                av_opt_set_int(swr_ctx, "out_channel_layout", audio_ctx->channel_layout, 0);
                av_opt_set_int(swr_ctx, "out_sample_rate", audio_ctx->sample_rate, 0);
                av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                if (swr_init(swr_ctx) < 0) {
                    cout << "Error initializing audio resample" << endl;
                    return false;
                }
            }
            if (swr_convert(swr_ctx, dst_audio_data, aud_frame->nb_samples,
                (const uint8_t**)aud_frame->data, aud_frame->nb_samples) < 0) {
                cout << "Error during audio resample" << endl;
                return false;
            }
        }
        ndi_audio_frame.no_samples = aud_frame->nb_samples;
        ndi_audio_frame.timecode = aud_frame->pts * 10000;
        ndi_audio_frame.p_data = (int16_t*)dst_audio_data[0];
        NDIlib_util_send_send_audio_interleaved_16s(ndi_sender, &ndi_audio_frame);
        return true;
    }

    /**
    * Decodes media from queue and send NDI, maintains delay
    */
    int idx = 0;
    void DecodingThreadVoid() {

        while (decoding) {
            int64_t delta_min = 0;
            list<AudioVideoFrame> frames_to_send;
            {
                lock_guard<mutex> g(buffers_mutex);
                if (media_buffer.size()) {
                    for (auto i = media_buffer.begin(); i != media_buffer.end();) {
                        if (reset_decoding_offset) {
                            reset_decoding_offset = false;
                            decoding_offset = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count() - (*i).first;
                            perfmon.start();
                        }

                        uint64_t need_pts = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now().time_since_epoch()).count() - decoding_offset;
                        auto old_i = i;
                        old_i++;
                        int64_t delta = (*i).first - (need_pts + delay);
                        if (delta < -drop_buffer) {
                            cout << "Decoder can't keep up. Offset by " << -delta << "ms" << endl;
                        }
                        if (!ignore_timestamps) {
                            if (delta > 0) {
                                break;
                            }
                            else if (delta <= 0) {
                                frames_to_send.push_back(move((*i).second));
                            }
                        }
                        else {
                            frames_to_send.push_back(move((*i).second));
                        }
                        media_buffer.erase(i);
                        i = old_i;
                    }
                }
            }
            if (frames_to_send.size()) {
                for (auto& i : frames_to_send) {
                    if (i.type == AVMEDIA_TYPE_VIDEO) {
                        DecodeVideoAndSendNdi(i.is_keyframe, i.timestamp, i.composition_time, move(i.data));
                        if (print_fps) {
                            perfmon.process();
                            perfmon.printFps(1000);
                        }
                    }
                    else if (i.type == AVMEDIA_TYPE_AUDIO) {
                        DecodeAudioAndSendNdi(i.timestamp, move(i.data));
                    }
                }
            }
        }
    }

    void CleanupVideo() {
        lock_guard<recursive_mutex> g(decoder_mutex);
        lock_guard<mutex> g2(buffers_mutex);
        media_buffer.clear();
        hw_pix_fmt = AV_PIX_FMT_NONE;
        avcodec_free_context(&video_ctx);
        av_buffer_unref(&hw_device_ctx);
        hw_device_ctx = nullptr;
        av_freep(&dst_data[0]);
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
        av_frame_free(&sw_frame);
        if (ndi_video_frame.p_data) {
            delete[] ndi_video_frame.p_data;
            ndi_video_frame.p_data = nullptr;
        }
    }

    void CleanupAudio() {
        lock_guard<recursive_mutex> g(decoder_mutex);
        lock_guard<mutex> g2(buffers_mutex);
        media_buffer.clear();
        ndi_audio_frame.p_data = nullptr;
        if (dst_audio_data)
            av_freep(&dst_audio_data[0]);
        av_freep(&dst_audio_data);
        swr_free(&swr_ctx);
        avcodec_free_context(&audio_ctx);
    }

    bool InitAudio(librtmp::ClientParameters* params, rtmp_proto::AudioPacketAAC audio_pkt) {
        assert(audio_pkt.aac_packet_type == 0);
        lock_guard<recursive_mutex> g(decoder_mutex);
        CleanupAudio();
        const AVCodec* codec = nullptr;

        if (!params->samplerate) {
            cout << "Audio parameters do not contain samplerate. Getting from packet. Samplerate is ";
            switch (audio_pkt.d.sample_rate)
            {
            case 0:
                params->samplerate = 5500;
                break;
            case 1:
                params->samplerate = 11000;
                break;
            case 2:
                params->samplerate = 11000;
                break;
            case 3:
                params->samplerate = 44100;
                break;
            }
            cout << params->samplerate << endl;
        }
        if (!params->channels) {
            cout << "Audio parameters do not contain channels. Getting from packet. Channel count is ";
            if (audio_pkt.d.channels == 1) {
                params->channels = 2;
            }
            else {
                params->channels = 1;
            }
            cout << params->channels << endl;
        }
        codec = avcodec_find_decoder(RTMPAudioCodecToFFMpeg(params->audio_codec));
        if (!codec) {
            cout << "Codec not supported" << endl;
            return false;
        }
        auto extra_data = move(audio_pkt.audio_data_send);
        audio_ctx = avcodec_alloc_context3(codec);
        audio_ctx->sample_rate = params->samplerate;
        audio_ctx->channels = params->channels;
        audio_ctx->channel_layout = av_get_default_channel_layout(params->channels);
        audio_ctx->time_base = { 1,1000 };
        audio_ctx->profile = FF_PROFILE_AAC_MAIN;
        audio_ctx->extradata = (uint8_t*)av_malloc(extra_data.size());
        audio_ctx->extradata_size = extra_data.size();
        memcpy(audio_ctx->extradata, extra_data.data(), extra_data.size());
        if (avcodec_open2(audio_ctx, codec, NULL) < 0) {
            cout << "Error opening audio decoder" << endl;
            return false;
        }
        av_samples_alloc_array_and_samples(&dst_audio_data, &dst_audio_linesize, audio_ctx->channels,
            NDI_AUDIO_FRAMESIZE, AV_SAMPLE_FMT_S16, 0);

        ndi_audio_frame.sample_rate = audio_ctx->sample_rate;
        ndi_audio_frame.no_channels = audio_ctx->channels;

        cout << endl << "Inited audio" << endl <<
            "Key: " << params->key << endl <<
            "Samplerate: " << params->samplerate << endl <<
            "Channels: " << params->channels << endl << endl;;
        return true;
    }

    int InitHarwareVideoDecoder(const AVCodec* codec) {
        AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
#ifdef WIN32
        type = AV_HWDEVICE_TYPE_D3D11VA;
#else
        type = AV_HWDEVICE_TYPE_VAAPI;
#endif
        for (int i = 0;; i++) {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
            if (!config) {
                cout << "Supported GPU not found" << endl;
                return -1;
            }
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                config->device_type == type) {
                hw_pix_fmt = config->pix_fmt;
                break;
            }
        }
        sw_frame = av_frame_alloc();
        video_ctx->get_format = get_hw_format;
        if (hw_decoder_ctx_init(video_ctx, type, &hw_device_ctx))
            return -1;
        return 0;
    }

    bool InitVideo(const librtmp::ClientParameters* params, DATA_BYTES extra_data) {
        lock_guard<recursive_mutex> g(decoder_mutex);
        CleanupVideo();
        const AVCodec* codec = nullptr;
        if (!codec) {
            codec = avcodec_find_decoder(RTMPVideoCodecToFFMpeg(params->video_codec));
        }
        if (!codec) {
            cout << "Codec " << avcodec_get_name(RTMPVideoCodecToFFMpeg(params->video_codec)) << " not supported" << endl;
            return false;
        }
        video_ctx = avcodec_alloc_context3(codec);
        video_ctx->width = params->width;
        video_ctx->height = params->height;
        video_ctx->framerate = { params->framerate,1 };
        video_ctx->time_base = { 1,1000 };
        video_ctx->extradata = (uint8_t*)av_malloc(extra_data.size());
        video_ctx->extradata_size = extra_data.size();
        if (use_multithreading_cpu) {
            video_ctx->thread_count = std::thread::hardware_concurrency();
            video_ctx->thread_type = FF_THREAD_FRAME;
            cout << "Use multithreaded CPU. Threads: " << video_ctx->thread_count << endl;
        }
        memcpy(video_ctx->extradata, extra_data.data(), extra_data.size());

        if (use_hw) {
            cout << "Using a hardware decoder" << endl;
            if (InitHarwareVideoDecoder(codec) < 0) {
                cout << "Failed to create hardware decoder. Fallback to software" << endl;
            }
        }
        else {
            cout << "Using software decoder" << endl;
        }

        if (avcodec_open2(video_ctx, codec, NULL) < 0) {
            cout << "Error opening video decoder" << endl;
            return false;
        }
        if (av_image_alloc(dst_data, dst_linesize,
            video_ctx->width, video_ctx->height, AV_PIX_FMT_BGRA, 1) < 0) {
            return false;
        }

        cout << endl << "Inited video" << endl <<
            "Key: " << params->key << endl <<
            "Width: " << params->width << endl <<
            "Height: " << params->height << endl << endl;;
        return true;
    }

    bool SendVideo(bool key_frame, uint32_t composition_time, uint64_t timestamp, DATA_BYTES data) {
        lock_guard<recursive_mutex> g(decoder_mutex);
        if (!video_ctx) {
            cout << "Error video decoder not initialized" << endl;
            return false;
        }
        AudioVideoFrame vf;
        vf.is_keyframe = key_frame;
        vf.timestamp = timestamp;
        vf.type = AVMEDIA_TYPE_VIDEO;
        vf.composition_time = composition_time;
        vf.data = move(data);
        {
            lock_guard<mutex> g(buffers_mutex);
            media_buffer.insert(make_pair(timestamp, move(vf)));
        }
        return true;
    }

    bool SendAudio(uint64_t timestamp, DATA_BYTES data) {
        lock_guard<recursive_mutex> g(decoder_mutex);
        if (!audio_ctx) {
            cout << "Error audio decoder not initialized" << endl;
            return false;
        }
        AudioVideoFrame af;
        af.type = AVMEDIA_TYPE_AUDIO;
        af.timestamp = timestamp;
        af.data = move(data);
        {
            lock_guard<mutex> g(buffers_mutex);
            media_buffer.insert(make_pair(timestamp, move(af)));
        }
        return true;
    }
};

unordered_map<string, DecoderStruct*> decoders;

DecoderStruct* CreateDecoderForKey(string key) {
    if (decoders.find(key) == decoders.end()) {
        decoders.insert(make_pair(key, new DecoderStruct(key)));
    }
    return decoders.at(key);
}

void ClientVoid2(DataLayer* transport_level) {
    librtmp::RTMPEndpoint rtmp_endpoint(transport_level);
    librtmp::RTMPServerSession server_session(&rtmp_endpoint);
    bool key_checked = false;
    bool first_run = true;

    while (true) {
        auto message = server_session.GetRTMPMessage();
        auto params = server_session.GetClientParameters();
        if (!params->key.size()) {
            //if no key, terminate connection
            cout << "Streaming without key, terminate" << endl;
            goto terminate_session;
        }
        if (!key_checked && allowed_keys.size()) {
            if (allowed_keys.find(params->key) == allowed_keys.end()) {
                cout << "Forbidden key" << endl;
                goto terminate_session;
            }
        }
        key_checked = true;
        auto ds = CreateDecoderForKey(params->key);
        if (first_run) {
            ds->decoding_offset = UINT64_MAX;
            first_run = false;
        }
        switch (message.message_type)
        {
        case librtmp::RTMPMessageType::VIDEO:
        {
            if (message.video.d.avc_packet_type == 0) {
                ds->reset_decoding_offset = true;
                if (!ds->InitVideo(params, move(message.video.video_data_send))) {
                    cout << "Error initializing video decoder" << endl;
                    goto terminate_session;
                }
                break;
            }
            bool is_key = false;
            if (message.video.d.frame_type == 1)is_key = true;
            if (is_key) {
                is_key = is_key;
            }
            if (!ds->SendVideo(is_key, message.video.d.composition_time, message.timestamp, move(message.video.video_data_send))) {
                cout << "Error sending video" << endl;
                goto terminate_session;
            }
        }
        break;
        case librtmp::RTMPMessageType::AUDIO:
            if (message.audio.aac_packet_type == 0) {
                ds->reset_decoding_offset = true;
                if (!ds->InitAudio(params, move(message.audio))) {
                    cout << "Error initializing audio decoder" << endl;
                    goto terminate_session;
                }
                break;
            }
            if (!ds->SendAudio(message.timestamp, move(message.audio.audio_data_send))) {
                cout << "Error sending audio" << endl;
                goto terminate_session;
            }
            break;
        default:
            assert(false);
            break;
        }
    }
terminate_session:
    return;
}

struct ClientStruct {
    bool running_flag = true;
    std::shared_ptr<TCPNetwork> tcp_network;
};

void ClientVoid1(unique_ptr<ClientStruct> cs) {
    try {
        DataLayer* transport_level = cs->tcp_network.get();
        if (use_tls) {
#ifdef USE_OPENSSL
            OpenSSL_TLS_Server tls_server;
            auto tls_layer = tls_server.handshake(transport_level, cert_path.c_str(), key_path.c_str());
            transport_level = tls_layer.get();
            ClientVoid2(transport_level);
#endif
        }
        else {
            ClientVoid2(transport_level);
        }
    }
    catch (...) {

    }
    cs->tcp_network->destroy();
    cs->running_flag = false;
}


void ServerThread(TCPServer* server) {
    try {
        while (true) {
            unique_ptr<ClientStruct> cs = make_unique<ClientStruct>();
            cs->tcp_network = server->accept();
            auto th = thread(&ClientVoid1, move(cs));
            th.detach();
        }
    }
    catch (TCPNetworkException& e) {
        cout << "Network error" << endl;
    }
    catch (exception& e) {
        cout << "Unexpected error" << endl;
    }
    cout << "Server stopped" << endl;
}

int main(int argc, char** argv)
{
    std::setlocale(LC_ALL, "en_US.UTF-8");
    if (!NDIlib_initialize()) {
        cout << "NDI is not compatible with CPU" << endl;
        return 0;
    }

    cxxopts::Options options("RTMP2NDI", "Portable RTMP server. Receive any RTMP stream and sends it to NDI with key name.");
    options.add_options()
        ("p,port", "network port [1935]", cxxopts::value<uint16_t>())
        ("c,cert", "TLS public certificate filepath", cxxopts::value<std::string>())
        ("k,key", "TLS private key filepath", cxxopts::value<std::string>())
        ("l,allow_keys", "RTMP allowed key list filepath. Keys are separated by newline", cxxopts::value<std::string>())
        ("w,hw_decoder", "enable hardware decoding on supported GPU")
        ("d,delay", "in milliseconds. Keeps frames synced: buffers or drops frames to keep them synced. Default is 0 ms", cxxopts::value<uint16_t>())
        ("b,drop_buffer", "in milliseconds. Detects when decoder can't keep up with video. Default to 200 ms", cxxopts::value<uint16_t>())
        ("m,disable_multithread", "disable multithreading decoding on CPU. Decreases latency")
        ("f,print_fps", "print frames per second")
        ("i,ignore_ts", "ignore timestampes. Display all frames as soon as possible")
        ("h,help", "help");
    auto result = options.parse(argc, argv);
    if (result.count("help"))
    {
        cout << options.help() << endl;
        return 0;
    }
    cout << options.help() << endl << endl;
    set_optional_parameter<uint16_t>(result, port, "port");
    set_optional_parameter<string>(result, cert_path, "cert");
    set_optional_parameter<string>(result, key_path, "key");
    set_optional_parameter<string>(result, allow_keys_path, "allow_keys");
    set_optional_parameter<uint16_t>(result, delay, "delay");
    set_optional_parameter<uint16_t>(result, drop_buffer, "drop_buffer");

    if (result.count("hw_decoder")) { use_hw = true; }
    if (result.count("print_fps")) { print_fps = true; }
    if (result.count("disable_multithread")) { use_multithreading_cpu = false; }
    if (result.count("ignore_ts")) { ignore_timestamps = true; }

    if ((!cert_path.size() || !key_path.size()) && (cert_path.size() || cert_path.size())) {
        cout << "Error: set cert and key to enable TLS server" << endl;
        return 0;
    }
    if (cert_path.size())
        use_tls = true;
#ifndef USE_OPENSSL
    if (use_tls) {
        cout << "Tools was built without TLS support";
        return 0;
    }
#endif // !USE_OPENSSL

    if (allow_keys_path.size()) {
        ifstream key_list_file(allow_keys_path, ios_base::in);
        if (!key_list_file.is_open()) {
            cout << "Could not open keylist file: " << allow_keys_path << endl;
            return 0;
        }
        std::string key;
        while (!key_list_file.eof()) {
            getline(key_list_file, key);
            allowed_keys.insert(key);
        }
    }
    cout << "Using port: " << port << endl;
    cout << "Use TLS: ";
    if (use_tls)
        cout << "YES";
    else
        cout << "NO";
    cout << endl;
    cout << "Starting server..." << endl;
    try {
        TCPServer tcp_server(port);
        thread th(&ServerThread, &tcp_server);
        cout << "Started" << endl;
        this_thread::sleep_for(std::chrono::seconds(UINT_MAX));
    }
    catch (TCPNetworkException& net_exception) {
        cout << "Network error" << endl;
    }
    catch (exception&) {
        cout << "Unexpected error" << endl;
    }
    return 0;
}