#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

void TCPConnection::fill_and_send_segments(bool rst) {
    _sender.fill_window();

    if (_sender.segments_out().empty())
        _sender.send_empty_segment();

    queue<TCPSegment> &sender_queue = _sender.segments_out();
    while (!sender_queue.empty()) {
        TCPSegment &seg = sender_queue.front();

        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
        }
        seg.header().win = _receiver.window_size();

        if (sender_queue.size() == 1 && rst) {
            seg.header().rst = true;
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _active = false;
        }

        _segments_out.push(move(seg));
        sender_queue.pop();
    }
}

size_t TCPConnection::remaining_outbound_capacity() const {
    return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const {
    return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {
    return _receiver.unassembled_bytes();
}

size_t TCPConnection::time_since_last_segment_received() const {
    return _cur_time - _last_receive_time;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active)
        return;

    _last_receive_time = _cur_time;
    const TCPHeader &header = seg.header();

    _receiver.segment_received(seg);
    if (header.ack) {
        _sender.ack_received(header.ackno, header.win);
        // 如果不需要等待了（我是被动关闭方），然后对面ack了我发送的fin包，直接关闭
        if (!_linger_after_streams_finish && _sender.stream_in().eof() &&
             _sender.bytes_in_flight() == 0 && _receiver.stream_out().eof()) {
            _active = false;
            return;
        }
    }
    // 我还没结束呢，对面先结束了，我是被动关闭方，就不需要等待2MSL了
    if (header.fin && !_sender.stream_in().eof())
        _linger_after_streams_finish = false;
    
    if (header.rst) {
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _active = false;
    }
    // 只要对面发的包占序列号就需要回ack
    if (seg.length_in_sequence_space() > 0)
        fill_and_send_segments(false);
    // 如果对面发的是不占序号的纯ack包，但是我这里buffer里还有未发送的数据，那就把这些数据都发了（ack掉的那些包腾出来发送窗口的位置了）
    else if (header.ack && !_sender.stream_in().buffer_empty())
        fill_and_send_segments(false);
}

bool TCPConnection::active() const {
    return _active;
}

size_t TCPConnection::write(const string &data) {
    if (!_active)
        return 0;

    size_t bytes = _sender.stream_in().write(data);
    fill_and_send_segments(false);
    return bytes;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _cur_time += ms_since_last_tick;
    // 我是主动关闭方，最后一个ack发送后等待完2MSL时间了，安全关闭
    if (_linger_after_streams_finish && _sender.bytes_in_flight() == 0 && _sender.stream_in().eof()
            && _receiver.stream_out().eof() && time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
        _active = false;
        return;
    }
    
    _sender.tick(ms_since_last_tick);

    // 这里发包只是为了发出重传的包，加判断防止发送纯ack包
    if (!_sender.segments_out().empty())
        fill_and_send_segments(_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS);
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    fill_and_send_segments(false);
}

void TCPConnection::connect() {
    fill_and_send_segments(false);
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            fill_and_send_segments(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
