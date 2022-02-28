#include "tcp_timer.hh"

using namespace std;

TCPTimer::TCPTimer(const unsigned int retransmission_timeout)
    : _timer_run(false), _retransmission_timer(0), _retransmission_timeout(retransmission_timeout) {}

void TCPTimer::tick(const unsigned int tick) { _retransmission_timer += tick; }

// 定时器是否超时
bool TCPTimer::timeout() { return _retransmission_timer >= _retransmission_timeout; }

// 重置timer
// 即将timer置位为0
void TCPTimer::reset() { _retransmission_timer = 0; }

//
void TCPTimer::double_RTO() { _retransmission_timeout *= 2; }

void TCPTimer::reset_RTO(const unsigned int retransmission_timeout) {
    _retransmission_timeout = retransmission_timeout;
}

void TCPTimer::stop() { _timer_run = false; }

bool TCPTimer::running() const { return _timer_run; }

void TCPTimer::start() {
    _retransmission_timer = 0;
    _timer_run = true;
}