#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &header = seg.header();

    if (header.syn) {
        if (!_isn_valid) {
            _isn_valid = true;
            _isn = header.seqno;
        } else
            return;
    }

    if (!_isn_valid)
        return;

    if (header.fin) {
        if (_fin_valid || !_isn_valid)
            return;
        else
            _fin_valid = true;
    }
    // 减一是要减去syn占的一个字节，在重整串中标志位不占字节，但是在TCP流中标志位占字节
    uint64_t abs_seq_no = unwrap(header.seqno, _isn, _ckp);
    if (!header.syn)
        abs_seq_no--;

    _reassembler.push_substring(seg.payload().copy(), abs_seq_no, header.fin);
    _ckp = _reassembler.ackno() - 1;
}

std::optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_isn_valid) {
        return {};
    } else {
        return wrap(_reassembler.ackno() + _isn_valid + stream_out().input_ended(), _isn);
    }
}

size_t TCPReceiver::window_size() const {
    return stream_out().remaining_capacity();
}
