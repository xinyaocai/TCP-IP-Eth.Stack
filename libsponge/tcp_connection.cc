#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

void TCPConnection::fill_and_send_segments(bool must_send, bool rst) {
    _sender.fill_window();

    //must_send控制是不是必须至少发一个控制报文包（ACK、RST等不占序列号的控制报文）
    //有现成的包就把控制报文写进去，没有的话那就再单独开一个包
    if (must_send && _sender.segments_out().empty())
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

        // send_syn只有connect函数可以发，别的函数不许发起握手，除非监听状态下回应对面的SYN
        //if (send_syn == seg.header().syn || 
        //   (_receiver.ackno().has_value() && _sender.next_seqno_absolute()>0 && _sender.next_seqno_absolute() == _sender.bytes_in_flight()))
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
        fill_and_send_segments(true, false);
    // 如果对面发的是不占序号的纯ack包，但是我这里buffer里还有未发送的数据，那就把这些数据都发了（ack掉的那些包腾出来发送窗口的位置了）
    else if (header.ack && !_sender.stream_in().buffer_empty())
        fill_and_send_segments(false, false);
}

bool TCPConnection::active() const {
    return _active;
}

size_t TCPConnection::write(const string &data) {
    if (!_active)
        return 0;

    size_t bytes = _sender.stream_in().write(data);
    fill_and_send_segments(false, false);
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

    // 这里发包只是为了发出重传的包，加判断防止发送其他包（纯控制报文包）
    if (!_sender.segments_out().empty())
        fill_and_send_segments(false, _sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS);
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    fill_and_send_segments(false, false);
}

void TCPConnection::connect() {
    fill_and_send_segments(false, false);
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            fill_and_send_segments(true, true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
