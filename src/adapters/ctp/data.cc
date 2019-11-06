#include <tbb/concurrent_unordered_map.h>
#include <unordered_set>

#include "api/ThostFtdcMdApi.h"
#include "opentrade/logger.h"
#include "opentrade/market_data.h"

using Security = opentrade::Security;

class Data : public CThostFtdcMdSpi, public opentrade::MarketDataAdapter {
 public:
  ~Data();
  void Start() noexcept override;
  void Stop() noexcept override;
  void Reconnect() noexcept override;

 private:
  void SubscribeSync(const Security &sec) noexcept override;
  void Close();
  void OnFrontConnected() override;
  void OnFrontDisconnected(int reason) override;
  void OnRspUserLogin(CThostFtdcRspUserLoginField *rsp_user_login,
                      CThostFtdcRspInfoField *rsp_info, int request_id,
                      bool is_last) override;
  void OnRspError(CThostFtdcRspInfoField *rsp_info, int request_id,
                  bool is_last) override;
  void OnRspSubMarketData(CThostFtdcSpecificInstrumentField *instrument,
                          CThostFtdcRspInfoField *rsp_info, int request_id,
                          bool is_last) override;
  void OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *instrument,
                            CThostFtdcRspInfoField *rsp_info, int request_id,
                            bool is_last) override;
  void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *data) override;
  void OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField *instrument,
                           CThostFtdcRspInfoField *rsp_info, int request_id,
                           bool is_last) override;
  void OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField *instrument,
                             CThostFtdcRspInfoField *rsp_info, int request_id,
                             bool is_last) override;
  void OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *data) override;
  void OnHeartBeatWarning(int time_lapse) override;

 private:
  CThostFtdcMdApi *api_ = nullptr;
  std::string address_, broker_id_, user_id_, password_;
  tbb::concurrent_unordered_map<std::string, const Security *> instruments_;
};

Data::~Data() { api_->Release(); }

void Data::Start() noexcept {
  address_ = config("address");
  if (address_.empty()) {
    LOG_FATAL(name() << ": address not given");
  }

  broker_id_ = config("broker_id");
  if (broker_id_.empty()) {
    LOG_FATAL(name() << ": broker_id not given");
  }

  user_id_ = config("user_id");
  if (user_id_.empty()) {
    LOG_FATAL(name() << ": user_id not given");
  }

  password_ = config("password");
  if (password_.empty()) {
    LOG_FATAL(name() << ": password not given");
  }

  Reconnect();
}

void Data::Close() {
  connected_ = 0;
  if (api_) {
    api_->Join();
    api_->RegisterSpi(NULL);
    api_->Release();
  }
}

void Data::Stop() noexcept {
  tp_.AddTask([this]() { Close(); });
}

void Data::Reconnect() noexcept {
  tp_.AddTask([this]() {
    Close();
    api_ = CThostFtdcMdApi::CreateFtdcMdApi();
    api_->RegisterSpi(this);
    LOG_INFO(name() << ": Connecting to " << address_);
    api_->RegisterFront(const_cast<char *>(address_.c_str()));
    api_->Init();
  });
}

void Data::SubscribeSync(const Security &sec) noexcept {
  char *req[1] = {const_cast<char *>(sec.local_symbol)};
  instruments_[sec.local_symbol] = &sec;
  api_->SubscribeMarketData(req, 1);
  // api_->SubscribeForQuoteRsp(req, 1);
}

void Data::OnHeartBeatWarning(int time_lapse) {
  LOG_INFO(name() << ": OnHeartBeatWarning: time_lapse=" << time_lapse);
}

void Data::OnRspError(CThostFtdcRspInfoField *rsp_info, int request_id,
                      bool is_last) {
  LOG_ERROR(name() << ": OnRspError, errorCode=" << rsp_info->ErrorID
                   << ", errorMsg=" << rsp_info->ErrorMsg
                   << ", requestId=" << request_id << ", chain=" << is_last);
}

void Data::OnRspSubMarketData(CThostFtdcSpecificInstrumentField *instrument,
                              CThostFtdcRspInfoField *rsp_info, int request_id,
                              bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    LOG_ERROR(name() << ": OnRspSubMarketData, errorCode=" << rsp_info->ErrorID
                     << ", errorMsg=" << rsp_info->ErrorMsg
                     << ", requestId=" << request_id << ", chain=" << is_last);
    return;
  }
  LOG_DEBUG(name() << ": Subscribed to market data of "
                   << instrument->InstrumentID);
}

void Data::OnRspSubForQuoteRsp(CThostFtdcSpecificInstrumentField *instrument,
                               CThostFtdcRspInfoField *rsp_info, int request_id,
                               bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    LOG_ERROR(name() << ": OnRspSubForQuoteRsp, errorCode=" << rsp_info->ErrorID
                     << ", errorMsg=" << rsp_info->ErrorMsg
                     << ", requestId=" << request_id << ", chain=" << is_last);
    return;
  }
  LOG_DEBUG(name() << ": Subscribed to quote of " << instrument->InstrumentID);
}

void Data::OnRspUnSubMarketData(CThostFtdcSpecificInstrumentField *instrument,
                                CThostFtdcRspInfoField *rsp_info,
                                int request_id, bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    LOG_ERROR(name() << ": OnRspUnSubMarketData, errorCode="
                     << rsp_info->ErrorID << ", errorMsg=" << rsp_info->ErrorMsg
                     << ", requestId=" << request_id << ", chain=" << is_last);
    return;
  }
  LOG_DEBUG(name() << ": Unsubscribed to market data of "
                   << instrument->InstrumentID);
}

void Data::OnRspUnSubForQuoteRsp(CThostFtdcSpecificInstrumentField *instrument,
                                 CThostFtdcRspInfoField *rsp_info,
                                 int request_id, bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    LOG_ERROR(name() << ": OnRspUnSubForQuoteRsp, errorCode="
                     << rsp_info->ErrorID << ", errorMsg=" << rsp_info->ErrorMsg
                     << ", requestId=" << request_id << ", chain=" << is_last);
    return;
  }
  LOG_DEBUG(name() << ": Unsubscribed to quote of "
                   << instrument->InstrumentID);
}

void Data::OnRtnDepthMarketData(CThostFtdcDepthMarketDataField *data) {
  if (!data) return;
  auto sec = instruments_[data->InstrumentID];
  if (!sec) return;
  using Quote = opentrade::MarketData::Quote;
  Update(sec->id, data->LastPrice, data->Volume, data->OpenPrice,
         data->HighestPrice, data->LowestPrice, data->AveragePrice);
  Update(sec->id,
         Quote{data->AskPrice1, data->BidPrice1, data->AskVolume1,
               data->BidVolume1},
         0);
  Update(sec->id,
         Quote{data->AskPrice2, data->BidPrice2, data->AskVolume2,
               data->BidVolume2},
         1);
  Update(sec->id,
         Quote{data->AskPrice3, data->BidPrice3, data->AskVolume3,
               data->BidVolume3},
         2);
  Update(sec->id,
         Quote{data->AskPrice4, data->BidPrice4, data->AskVolume4,
               data->BidVolume4},
         3);
  Update(sec->id,
         Quote{data->AskPrice5, data->BidPrice5, data->AskVolume5,
               data->BidVolume5},
         4);
}

void Data::OnRtnForQuoteRsp(CThostFtdcForQuoteRspField *data) {}

void Data::OnFrontConnected() {
  CThostFtdcReqUserLoginField login{};
  strncpy(login.BrokerID, broker_id_.c_str(), sizeof(login.BrokerID) - 1);
  strncpy(login.UserID, user_id_.c_str(), sizeof(login.UserID) - 1);
  strncpy(login.Password, password_.c_str(), sizeof(login.Password) - 1);
  // 发出登陆请求
  LOG_INFO(name() << ": Connected, send login");
  api_->ReqUserLogin(&login, ++request_counter_);
}

// 当客户端与交易托管系统通信连接断开时，该方法被调用
void Data::OnFrontDisconnected(int reason) {
  // 当发生这个情况后，API会自动重新连接，客户端可不做处理
  LOG_ERROR(name() << ": Disconnected, reason=" << reason);
  connected_ = 0;
}

// 当客户端发出登录请求之后，该方法会被调用，通知客户端登录是否成功
void Data::OnRspUserLogin(CThostFtdcRspUserLoginField *rsp_user_login,
                          CThostFtdcRspInfoField *rsp_info, int request_id,
                          bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    // 端登失败，客户端需进行错误处理
    LOG_ERROR(name() << ": Failed to login, errorCode=" << rsp_info->ErrorID
                     << ", errorMsg=" << rsp_info->ErrorMsg
                     << " requestId=" << request_id << ", chain=" << is_last);
    return;
  }
  tp_.AddTask([this]() {
    connected_ = 1;
    ReSubscribeAll();
  });
  LOG_INFO(name() << ": User logged in");
}

extern "C" {
opentrade::Adapter *create() { return new Data{}; }
}
