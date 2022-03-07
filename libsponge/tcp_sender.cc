#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{std::random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _tcptimer(retx_timeout) {}

// ����һ��TCP���İ�
void TCPSender::send_tcpsegment(TCPSegment &seg) {
    seg.header().seqno = next_seqno();
    // ��segment����_segments_out��Ϊ����
    _segments_out.push(seg);
    // ÿ����һ��segment,�ڲ���¼
    _segments_outstanding.push(seg);
    _next_seqno += static_cast<uint64_t>(seg.length_in_sequence_space());

    // ÿ������һ���������ݵ�segement֮�����timer��������
    // �Ա���RTOʱ���expires
    if (!_tcptimer.running()) {
        _tcptimer.reset();
        _tcptimer.start();
    }
}

void TCPSender::fill_window() {
    // FIN_SENT OR FIN_ACKED
    if (stream_in().eof() && next_seqno_absolute() == stream_in().bytes_written() + 2)
        return;

    TCPSegment tcpsegment;
    // send a syn segment for handshake
    // CLOSED STATE --- waitting for stream to begin(no SYN sent)
    // ����SYNʱ����Я���κ�����
    if (next_seqno_absolute() == 0) {
        tcpsegment.header().syn = true;
        send_tcpsegment(tcpsegment);
        return;
    }

    // SYN_SENT STATE ---- stream started but nothing acknowledged
    if (!_segments_outstanding.empty() && _segments_outstanding.front().header().syn)
        return;

    // 1. ��δ�յ�receiver��ACK,��_recv_winsize��ʼΪ0
    // 2. �յ�receiver��ACK��֪���ڴ�СΪ0
    // �����շ����ڴ�СΪ0ʱ,fill_window()���������ڴ�С��Ϊ1
    uint64_t recv_winsize = _recv_winsize > 0 ? _recv_winsize : 1;

    // ʣ�¿ɷ������ݴ�С = ���ڴ�С - �ѷ��͵���δ�����ֽ���
    // !!!!!!!!!! �˴�����Խ��Ŀ���  !!!!!!!!!!
    uint64_t recv_remain_winsize = recv_winsize > bytes_in_flight() ? recv_winsize - bytes_in_flight() : 0;
    // receiver������ʣ��Ĵ��ڿ��Խ�������
    while (recv_remain_winsize > 0) {
        // TCPSender������input ByteStream�ж�ȡ����
        // ���������ܶ�����ݷŵ�TCPSegment��
        uint64_t read_size = std::min(static_cast<uint64_t>(recv_remain_winsize), TCPConfig::MAX_PAYLOAD_SIZE);
        tcpsegment.payload() = stream_in().read(read_size);

        // ���㹻�Ŀռ�����fin
        // syn��fin��ռ��һ�����ڴ�С
        if (stream_in().eof() && tcpsegment.length_in_sequence_space() < recv_remain_winsize) {
            tcpsegment.header().fin = true;
        }

        // empty segment
        if (tcpsegment.length_in_sequence_space() == 0)
            return;
        recv_remain_winsize -= tcpsegment.length_in_sequence_space();
        send_tcpsegment(tcpsegment);
        if (tcpsegment.header().fin)
            return;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // �õ���ǰ�������ݰ���absolute seqno
    uint64_t abs_ackseqno = unwrap(ackno, _isn, _recv_ackno);

    // abs_ackseqno <= _recv_ackno ---- ��ackno֮ǰ�������Ѿ�ȷ��
    // abs_ackseqno > _next_seqno ---- ��Ч��ackno
    if (abs_ackseqno <= _recv_ackno || abs_ackseqno > next_seqno_absolute()) {
        //  t_send_window.cc����һֻ����window size�Ĳ���
        if (abs_ackseqno == _recv_ackno)
            _recv_winsize = window_size;
        return;
    }

    // ---- �����ǽ��յ�new data�����ȡ�Ĳ��� -----
    // ����receiver�Ľ��մ��ڴ�С
    _recv_winsize = window_size;

    // the ackno reflects an absolute sequence number bigger than any previous ackno
    // a. set the RTO back to its ��initial value.��
    _tcptimer.reset_RTO(_initial_retransmission_timeout);

    // �����Ѿ����յ�seqno
    _recv_ackno = abs_ackseqno;

    TCPSegment tcpsegment;
    // ��outstanding segments���м��
    // ���Ѿ���ȫ���յ���segemnts�����Ƴ�
    while (!_segments_outstanding.empty()) {
        tcpsegment = _segments_outstanding.front();

        // fully accpeted
        // the ackno is greater than all of the sequence numbers in the segment
        if (unwrap(tcpsegment.header().seqno, _isn, _recv_ackno) + tcpsegment.length_in_sequence_space() <=
            abs_ackseqno) {
            _segments_outstanding.pop();
        } else {
            break;
        }
    }

    if (_segments_outstanding.empty()) {
        _tcptimer.stop();
    } else {
        // b. restart the retransmission timer
        _tcptimer.reset();
    }
    // 3. reset the count of ��consecutive retransmissions�� back to zero.
    _consecutive_retransmissions = 0;
    fill_window();
    return;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (!_tcptimer.running())
        return;
    // tcptimer�еĶ�ʱ������ms_since_last_tick
    // ʱ���ȥms_since_last_tick
    _tcptimer.tick(ms_since_last_tick);

    if (_tcptimer.timeout() && !_segments_outstanding.empty()) {
        // ��tick��������retransmission timer expiredʱ
        // a. �ش�û�б�TCPReceiver��ȫ���յ�segments
        _segments_out.push(_segments_outstanding.front());

        // b. window size is nonzero
        // i. ׷�������ش�����Ŀ�������ش�segment֮������������Ŀ
        // ii. ��RTO��ֵ���� ---- ָ���˱��㷨
        //
        if (_segments_outstanding.front().header().syn || _recv_winsize != 0) {
            _consecutive_retransmissions++;
            _tcptimer.double_RTO();
        }

        // Every time a segment containing data is sent,if the timer is not running,start it
        _tcptimer.reset();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment tcpsegment;
    tcpsegment.header().seqno = next_seqno();
    // empty segment��ռ�����,����Ҫ�����ش�
    _segments_out.push(tcpsegment);
}
