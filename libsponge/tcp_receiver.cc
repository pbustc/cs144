#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &tcpheader = seg.header();
    // 判断是否是SYN之前的数据
    if (!_syn) {
        // message without SYN
        if (!tcpheader.syn)
            return;
        _syn = true;
        _isn = tcpheader.seqno;
    }

    /* 此处纯属多此一举*/
    // 当payload().size() == 0 --- 无数据
    // SYN == 1,FIN == 1时，不能正常终止
    // if (seg.payload().size() == 0)
    //     return;

    // 调用StreamReassembler中的push_substring
    // \param data the substring  --- seg._payload._storage
    // \param index absolute seqno --- unwrap(seqno,_isn,checkpoint)
    // \param eof the last byte of `data` --- tcpheader.fin

    // 需要计算得到stream index,先通过seqno计算得到absolute seqno
    // 再通过absolute seqno计算得到stream index
    // SYN占一个seqno,但不占用payload,计算first_unread的absolute seqno
    uint64_t _checkpoint = _reassembler.stream_out().bytes_written() + 1;

    // SYN message ---  SYN 和 FIN 标志各占一个 seqno
    // 计算得到当前TCP报文第一个字符的absolute seqno
    // index --- zero-based
    uint64_t abs_seqno = unwrap(tcpheader.seqno, _isn, _checkpoint);
    // 计算得到stream index

    uint64_t stream_index = abs_seqno - 1 + static_cast<uint64_t>(tcpheader.syn);
    _reassembler.push_substring(seg.payload().copy(), stream_index, tcpheader.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // 返回receiver下一个需要接收字节的seqno
    // stream index ---> absolute seqno ---> seqno
    if (!_syn)
        return nullopt;
    // get stream index
    uint64_t stream_index = _reassembler.stream_out().bytes_written();

    // calculate absolute seqno
    uint64_t abs_seqno = stream_index + 1;
    // add FIN seqno
    if (_reassembler.stream_out().input_ended())
        ++abs_seqno;
    return wrap(abs_seqno, _isn);
}

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
