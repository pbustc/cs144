class TCPTimer {
  private:
    // 当前TCPTimer是否运行
    bool _timer_run;
    // 记录TCPTimer运行的时间
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