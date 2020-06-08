/*
* Copyright (c) 2018-present, aliminabc@gmail.com.
*
* This source code is licensed under the MIT license found in the
* LICENSE file in the root directory of this source tree.
*/

#include "../include/HwFFMuxer.h"
#include "Logcat.h"

#define TAG "HwFFMuxer"

HwFFMuxer::HwFFMuxer() : HwAbsMuxer() {
}

HwFFMuxer::~HwFFMuxer() {
    release();
}

void HwFFMuxer::release() {
    if (pFormatCtx) {
        if (started && 0 == av_write_trailer(pFormatCtx)) {
            AlLogI(TAG, "close file success.");
        } else {
            AlLogE(TAG, "close file failed!");
        }
        if (!(pFormatCtx->flags & AVFMT_NOFILE)) {
            if (0 != avio_closep(&pFormatCtx->pb)) {
                AlLogE(TAG, "close file failed!");
            }
        }
        avformat_free_context(pFormatCtx);
        pFormatCtx = nullptr;
    }
    tracks.clear();
    started = false;
}

HwResult HwFFMuxer::configure(string filePath, string type) {
    HwAbsMuxer::configure(filePath, type);
    av_register_all();
    int ret = avformat_alloc_output_context2(&pFormatCtx, NULL, type.c_str(), filePath.c_str());
    if (ret < 0 || !pFormatCtx) {
        AlLogE(TAG, "failed %s", strerror(AVUNERROR(ret)));
        return Hw::FAILED;
    }
    av_dict_set(&pFormatCtx->metadata, "comment", "hwvc", 0);
    avPacket = av_packet_alloc();
    return Hw::SUCCESS;
}

HwResult HwFFMuxer::start() {
    if (avio_open2(&pFormatCtx->pb, filePath.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr) < 0) {
        AlLogE(TAG, "failed to open output file!");
        release();
        return Hw::FAILED;
    }
    int ret = avformat_write_header(pFormatCtx, nullptr);
    if (ret < 0) {
        AlLogE(TAG, "failed to write header, %s", strerror(AVUNERROR(ret)));
        release();
        return Hw::FAILED;
    }
    started = true;
    return Hw::SUCCESS;
}

void HwFFMuxer::setInt32Parameter(int32_t &param, int32_t value) {
    if (HwBundle::VALUE_NONE != value) {
        param = value;
    }
}

void HwFFMuxer::setInt64Parameter(int64_t &param, int64_t value) {
    if (HwBundle::VALUE_NONE != value) {
        param = value;
    }
}

int32_t HwFFMuxer::addTrack(AlCodec *codec) {
    if (!codec) {
        return TRACK_NONE;
    }
    AVStream *stream = avformat_new_stream(pFormatCtx, nullptr);
    if (!stream) {
        return TRACK_NONE;
    }

    // See avcodec_parameters_from_context
    stream->codecpar->codec_id = static_cast<AVCodecID>(codec->getCodecID());
    setInt32Parameter(stream->codecpar->profile,
                      codec->getFormat().getInt32(AlCodec::KEY_PROFILE));
    setInt32Parameter(stream->codecpar->level, codec->getFormat().getInt32(AlCodec::KEY_LEVEL));
    setInt64Parameter(stream->codecpar->bit_rate,
                      codec->getFormat().getInt32(AlCodec::KEY_BIT_RATE));
    if (AlCodec::kMediaType::AUDIO == codec->getMediaType()) {
        stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        setInt32Parameter(stream->codecpar->sample_rate,
                          codec->getFormat().getInt32(AlCodec::KEY_SAMPLE_RATE));
        setInt32Parameter(stream->codecpar->channels,
                          codec->getFormat().getInt32(AlCodec::KEY_CHANNELS));
        setInt32Parameter(stream->codecpar->frame_size,
                          codec->getFormat().getInt32(AlCodec::KEY_FRAME_SIZE));
        stream->codecpar->channel_layout = static_cast<uint64_t>(
                av_get_default_channel_layout(stream->codecpar->channels));
        stream->codecpar->format = AV_SAMPLE_FMT_FLTP;
//        stream->time_base = {1, stream->codecpar->sample_rate};
    } else if (AlCodec::kMediaType::VIDEO == codec->getMediaType()) {
        stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        setInt32Parameter(stream->codecpar->width,
                          codec->getFormat().getInt32(AlCodec::KEY_WIDTH));
        setInt32Parameter(stream->codecpar->height,
                          codec->getFormat().getInt32(AlCodec::KEY_HEIGHT));
        stream->codecpar->format = AV_PIX_FMT_YUV420P;
//        stream->time_base = {1, codec->getFormat()->getInt32(HwAbsCodec::KEY_FPS)};
    }
    copyExtraData(stream, codec);
    tracks.push_back(stream);
    return tracks.size() - 1;
}

bool HwFFMuxer::copyExtraData(AVStream *stream, AlCodec *codec) {
    auto *buf = codec->getExtraData();
    if (buf && buf->size() > 0) {
        buf->rewind();
        stream->codecpar->extradata_size = static_cast<int>(buf->remaining());
        stream->codecpar->extradata = static_cast<uint8_t *>(av_mallocz(buf->remaining()));
        buf->get(stream->codecpar->extradata, buf->remaining());
        AlLogI(TAG, "Copy extra data size(%d)", buf->remaining());
//                FILE *fp = fopen("/sdcard/extra.data", "wb");
//                fwrite(stream->codecpar->extradata, 1, stream->codecpar->extradata_size, fp);
//                fclose(fp);
    } else {
        if (AV_CODEC_ID_H264 == stream->codecpar->codec_id ||
            AV_CODEC_ID_AAC_LATM == stream->codecpar->codec_id ||
            AV_CODEC_ID_AAC == stream->codecpar->codec_id) {
            assert(false);
        }
        return false;
    }
    return true;
}

HwResult HwFFMuxer::write(int32_t track, HwPacket *pkt) {
    if (!started || !pkt || track > tracks.size() - 1 || !pFormatCtx) {
        return Hw::FAILED;
    }
    pkt->ref(&avPacket);
    avPacket->stream_index = tracks[track]->index;
    AVRational tb = tracks[track]->time_base;
    // pts / cq * bq
    av_packet_rescale_ts(avPacket, AV_TIME_BASE_Q, tb);
    if (tracks[track]->cur_dts >= avPacket->dts) {
        AlLogE(TAG, "will failed cur_dts(%lld), dts(%lld), idx(%d)), try reset correctly.",
               tracks[track]->cur_dts, avPacket->dts, avPacket->stream_index);
        avPacket->dts += 1;
    }
    int ret = av_interleaved_write_frame(pFormatCtx, avPacket);
    if (0 != ret) {
        AlLogE(TAG, "failed(track %d): %s", track, strerror(AVUNERROR(ret)));
        return Hw::FAILED;
    }
    return Hw::SUCCESS;
}