#include "stream_reassembler.hh"

#include <vector>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) :
_wait_idx(0), _eof_idx(INT64_MAX), _output(capacity), _capacity(capacity), _cache(){}

void StreamReassembler::copy_to_cache(const string &data, size_t begin_idx) {
    size_t dup = 0, i = 0;
    for (; i < data.size(); i++) {
        if (_cache.count(begin_idx+i) && data[i] == _cache[begin_idx+i]) {
            dup++;
        } else {
            _cache[begin_idx+i] = data[i];
        }
    }
    _unassem_bytes += i-dup;
    _receive_bytes += i-dup;
}

void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 如果当前eof为真，那就计算保存eof的序列号
    if (eof) 
        _eof_idx = index + data.size();
    string to_write;
    if (!data.empty()) {
        if (_wait_idx > index) {
            // 检测overlap
            for (size_t i = index; i < _wait_idx; i++)
                if (_cache.count(i) && _cache[i] != data[i-index])
                    return;
            // 存在overlap，但是当前data只是已重整串的子串，不需要append
            if (index + data.size() <= _wait_idx)
                return;
            // 存在overlap，写入overlap部分之外的子串
            to_write = data.substr(_wait_idx - index);
            copy_to_cache(to_write, _wait_idx);
        } else {
            //data是当前所需串或者是未来串的情况
            to_write = data;
            copy_to_cache(to_write, index);
            // 如果是未来串，那保存好了就可以返回了
            if (_wait_idx < index) return;
        }
        // 此时_wait_idx == index，将串写入输出中
        while (_wait_idx < _eof_idx) {
            size_t write_num = _output.write(to_write);
            _wait_idx += write_num;
            _unassem_bytes -= write_num;
            if (write_num==0 || !_cache.count(_wait_idx)) break;
            to_write.clear();
            for (size_t i = _wait_idx; i < _eof_idx && _cache.count(i); i++) {
                to_write += _cache[i];
            }
        }
    }
    if (_wait_idx >= _eof_idx)
        _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const {
    return _unassem_bytes;
}

bool StreamReassembler::empty() const {
    return _wait_idx>=_eof_idx;
}