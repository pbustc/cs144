class TCPTimer {
  private:
    // ��ǰTCPTimer�Ƿ�����
    bool _timer_run;
    // ��¼TCPTimer���е�ʱ��
    unsigned int _retransmission_timer;
    // RTO
    unsigned int _retransmission_timeout;

  public:
    TCPTimer(const unsigned int retransmission_timeout);
    void tick(const unsigned int tick);
    bool timeout();
    void reset();
    void double_RTO();
    void reset_RTO(const unsigned int retransmission_timeout);
    void stop();
    bool running() const;
    void start();
};