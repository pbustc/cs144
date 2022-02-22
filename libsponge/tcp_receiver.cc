#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader &tcpheader = seg.header();
    // �ж��Ƿ���SYN֮ǰ������
    if (!_syn) {
        // message without SYN
        if (!tcpheader.syn)
            return;
        _syn = true;
        _isn = tcpheader.seqno;
    }

    /* �˴��������һ��*/
    // ��payload().size() == 0 --- ������
    // SYN == 1,FIN == 1ʱ������������ֹ
    // if (seg.payload().size() == 0)
    //     return;

    // ����StreamReassembler�е�push_substring
    // \param data the substring  --- seg._payload._storage
    // \param index absolute seqno --- unwrap(seqno,_isn,checkpoint)
    // \param eof the last byte of `data` --- tcpheader.fin

    // ��Ҫ����õ�stream index,��ͨ��seqno����õ�absolute seqno
    // ��ͨ��absolute seqno����õ�stream index
    // SYNռһ��seqno,����ռ��payload,����first_unread��absolute seqno
    uint64_t _checkpoint = _reassembler.stream_out().bytes_written() + 1;

    // SYN message ---  SYN �� FIN ��־��ռһ�� seqno
    // ����õ���ǰTCP���ĵ�һ���ַ���absolute seqno
    // index --- zero-based
    uint64_t abs_seqno = unwrap(tcpheader.seqno, _isn, _checkpoint);
    // ����õ�stream index

    uint64_t stream_index = abs_seqno - 1 + static_cast<uint64_t>(tcpheader.syn);
    _reassembler.push_substring(seg.payload().copy(), stream_index, tcpheader.fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // ����receiver��һ����Ҫ�����ֽڵ�seqno
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
