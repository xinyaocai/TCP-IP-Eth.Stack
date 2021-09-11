#include "tcp_sender.hh"

#include "tcp_config.hh"

#include<iostream>

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _consec_retx_count(0) 
    , _retx_timeout(retx_timeout)
    , _cur_window_size(1)
    , _curr_timestamp(0)
    , _segments_fly()
    , _initial_retransmission_timeout{retx_timeout}
    , _eof(false)
    , _stream(capacity)
    , _try_window(false) {}

uint64_t TCPSender::bytes_in_flight() const {
    return std::accumulate(_segments_fly.begin(), _segments_fly.end(), 0, [](int accu, auto &b){
        return accu + b.segment.length_in_sequence_space();
    });
}

void TCPSender::fill_window() {
    while (true) {
        size_t send_size = 0;
        TCPSegment segment;
        TCPHeader &header = segment.header();

        header.seqno = wrap(_next_seqno, _isn);
        if (_cur_window_size>0 && _next_seqno==0) {
            header.syn = true;
            send_size++;
        }

        size_t payload_size = min(min(TCPConfig::MAX_PAYLOAD_SIZE-send_size, _stream.buffer_size()), _cur_window_size-send_size);

        segment.payload() = Buffer(_stream.peek_output(payload_size));
        _stream.pop_output(payload_size);
        send_size += payload_size;

        if (!_eof && _stream.eof() && _cur_window_size > send_size) {
            header.fin = true;
            send_size++;
            _eof = true;
        }

        if (send_size == 0)
            return;
        // copy?
        _segments_out.push(segment);
        _segments_fly.emplace_back(_curr_timestamp, segment);
        _next_seqno += send_size;
        _cur_window_size -= send_size;
    }  
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 没有ACK的包定时器会自动重传，不需要在这里调整_next_seqno
    uint64_t abs_ackno = unwrap(ackno, _isn, _next_seqno);
    if (abs_ackno > _next_seqno)
        return;
    
    size_t sz1 = _segments_fly.size();

    _segments_fly.erase(remove_if(_segments_fly.begin(), _segments_fly.end(), [this, abs_ackno](auto &it){
        return unwrap(it.segment.header().seqno, _isn, _next_seqno) + it.segment.length_in_sequence_space() <= abs_ackno;
    }), _segments_fly.end());

    // really ack something
    if (_segments_fly.size() < sz1) {
        _retx_timeout = _initial_retransmission_timeout;
        _consec_retx_count = 0;

        for (auto &it : _segments_fly) {
            it.time_stamp = _curr_timestamp;
        }
    }

    if (window_size == 0)
        _try_window = true;
    else if (_try_window)
        _try_window = false;

    _cur_window_size = max(abs_ackno + max(window_size, static_cast<uint16_t>(1)) - _next_seqno, static_cast<uint64_t>(0));
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _curr_timestamp += ms_since_last_tick;

    if (!_segments_fly.empty() && _curr_timestamp - _segments_fly.front().time_stamp >= _retx_timeout) {
        if (!_try_window)
            _retx_timeout *= 2;
        _segments_out.push(_segments_fly.front().segment);
        _segments_fly.front().time_stamp = _curr_timestamp;
        _consec_retx_count++;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const {
    return _consec_retx_count;
}

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(segment);
}
