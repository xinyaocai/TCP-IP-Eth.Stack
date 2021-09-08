#include "byte_stream.hh"

#include <iostream>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <vector>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

using namespace std;

ByteStream::ByteStream(const size_t capacity): _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    if (!_is_open) return 0;
    size_t i = 0;
    for (; i<data.size() && _buffer.size()<_capacity; i++) {
        _buffer.push_back(data[i]);
        _num_written++;
    }
    return i;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string ans;
    for(size_t i = 0; i<len && i<_buffer.size(); i++) {
        ans += _buffer[i];
    }
    return ans;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t max_pop_length = min(len, _buffer.size());
    for(size_t i = 0; i<max_pop_length; i++) {
        _buffer.pop_front();
        _num_popped++;
    }
}

void ByteStream::end_input() {
    _is_open = false;
}

bool ByteStream::input_ended() const {
    return !_is_open;
}

size_t ByteStream::buffer_size() const {
    return _buffer.size();
}

bool ByteStream::buffer_empty() const {
    return _buffer.size()==0;
}

bool ByteStream::eof() const {
    return input_ended() && buffer_empty();
}

size_t ByteStream::bytes_written() const {
    return _num_written;
}

size_t ByteStream::bytes_read() const {
    return _num_popped;
}

size_t ByteStream::remaining_capacity() const {
    return _capacity - _buffer.size();
}
