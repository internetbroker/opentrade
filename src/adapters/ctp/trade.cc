#include <tbb/concurrent_unordered_map.h>

#include "api/ThostFtdcTraderApi.h"
#include "opentrade/exchange_connectivity.h"
#include "opentrade/logger.h"
#include "opentrade/task_pool.h"

class Trade : public CThostFtdcTraderSpi,
              public opentrade::ExchangeConnectivityAdapter {
 public:
  ~Trade();
  void Start() noexcept override;
  void Stop() noexcept override;
  void Reconnect() noexcept override;
  std::string Place(const opentrade::Order& ord) noexcept override;
  std::string Cancel(const opentrade::Order& ord) noexcept override;

 private:
  void Close();
  void OnFrontConnected() override;
  void OnFrontDisconnected(int reason) override;
  void OnRspUserLogin(CThostFtdcRspUserLoginField* rsp_user_login,
                      CThostFtdcRspInfoField* rsp_info, int request_id,
                      bool is_last) override;
  void OnRspError(CThostFtdcRspInfoField* rsp_info, int request_id,
                  bool is_last) override;
  void OnRspOrderInsert(CThostFtdcInputOrderField* input_order,
                        CThostFtdcRspInfoField* rsp_info, int request_id,
                        bool is_last) override;
  void OnRtnOrder(CThostFtdcOrderField* ord) override;
  void OnRtnTrade(CThostFtdcTradeField* trd) override;
  void OnHeartBeatWarning(int time_lapse) override;
  void OnRspAuthenticate(CThostFtdcRspAuthenticateField* rsp_auth_field,
                         CThostFtdcRspInfoField* rsp_info, int request_id,
                         bool is_last) override;
  void OnRspSettlementInfoConfirm(
      CThostFtdcSettlementInfoConfirmField* settlement_info,
      CThostFtdcRspInfoField* rsp_info, int request_id, bool is_last);
  void OnRspQrySettlementInfo(CThostFtdcSettlementInfoField* settlement_info,
                              CThostFtdcRspInfoField* rsp_info, int request_id,
                              bool is_last) override;
  void Login();
  void Auth();

 private:
  CThostFtdcTraderApi* api_ = nullptr;
  std::string address_, broker_id_, user_id_, password_;
  std::string product_info_, auth_code_, app_id_;
  struct Order {
    int front_id = -1;
    int session_id = -1;
    int seq_num = -1;
  };
  tbb::concurrent_unordered_map<unsigned, Order> orders_;
  std::ofstream of_;
  opentrade::TaskPool tp_;
  std::atomic<int> request_counter_ = 0;
};

Trade::~Trade() { api_->Release(); }

void Trade::Start() noexcept {
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

  product_info_ = config("product_info");
  auth_code_ = config("auth_code");
  app_id_ = config("app_id");

  auto path = opentrade::kStorePath / (name() + "-session");
  std::ifstream ifs(path.c_str());
  if (ifs.good()) {
    auto n = 0;
    std::string line;
    while (std::getline(ifs, line)) {
      if (line.empty() || line.at(0) == '#') continue;
      int order_ref;
      int front_id;
      int session_id;
      int seq_num;
      if (sscanf(line.c_str(), "%d %d %d %d", &order_ref, &front_id,
                 &session_id, &seq_num) == 4) {
        auto& o = orders_[order_ref];
        o.front_id = front_id;
        o.session_id = session_id;
        o.seq_num = seq_num;
        n++;
      }
    }
    LOG_INFO(name() << ": #" << n << " offline orders loaded");
  }
  of_.open(path.c_str(), std::ofstream::app);
  if (!of_.good()) {
    LOG_FATAL(name() << ": Failed to write file '" << path
                     << "' : " << strerror(errno));
  }

  Reconnect();
}

void Trade::Close() {
  connected_ = 0;
  if (api_) {
    api_->Join();
    api_->RegisterSpi(NULL);
    api_->Release();
  }
}

void Trade::Stop() noexcept {
  tp_.AddTask([this]() { Close(); });
}

void Trade::Reconnect() noexcept {
  tp_.AddTask([this]() {
    Close();
    api_ = CThostFtdcTraderApi::CreateFtdcTraderApi();
    api_->RegisterSpi(this);
    LOG_INFO(name() << ": Connecting to " << address_);
    api_->RegisterFront(const_cast<char*>(address_.c_str()));
    api_->Init();
  });
}

void Trade::OnHeartBeatWarning(int time_lapse) {
  LOG_INFO(name() << ": OnHeartBeatWarning: time_lapse=" << time_lapse);
}

void Trade::OnRspError(CThostFtdcRspInfoField* rsp_info, int request_id,
                       bool is_last) {
  LOG_ERROR(name() << ": OnRspError, errorCode=" << rsp_info->ErrorID
                   << ", errorMsg=" << rsp_info->ErrorMsg
                   << ", requestId=" << request_id << ", chain=" << is_last);
}

#define STRCPY(a, b) strncpy(a, b, sizeof(a) - 1)

void Trade::OnFrontConnected() {
  if (!product_info_.empty() && !auth_code_.empty())
    Auth();
  else
    Login();
}

void Trade::Auth() {
  CThostFtdcReqAuthenticateField reqAuth{};
  STRCPY(reqAuth.BrokerID, broker_id_.c_str());
  STRCPY(reqAuth.UserID, user_id_.c_str());
  STRCPY(reqAuth.UserProductInfo, product_info_.c_str());
  STRCPY(reqAuth.AuthCode, auth_code_.c_str());
  STRCPY(reqAuth.AppID, app_id_.c_str());
  api_->ReqAuthenticate(&reqAuth, ++request_counter_);
}

void Trade::Login() {
  CThostFtdcReqUserLoginField login{};
  STRCPY(login.BrokerID, broker_id_.c_str());
  STRCPY(login.UserID, user_id_.c_str());
  STRCPY(login.Password, password_.c_str());
  // 发出登陆请求
  LOG_INFO(name() << ": Connected, send login");
  api_->ReqUserLogin(&login, ++request_counter_);
}

void Trade::OnRspAuthenticate(CThostFtdcRspAuthenticateField* rsp_auth_field,
                              CThostFtdcRspInfoField* rsp_info, int request_id,
                              bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    LOG_ERROR(name() << ": Failed to authenticate, errorCode="
                     << rsp_info->ErrorID << ", errorMsg=" << rsp_info->ErrorMsg
                     << " requestId=" << request_id << ", chain=" << is_last);

    tp_.AddTask([this]() { Auth(); }, boost::posix_time::seconds(60));
    return;
  }
  Login();
}

// 当客户端与交易托管系统通信连接断开时，该方法被调用
void Trade::OnFrontDisconnected(int reason) {
  // 当发生这个情况后，API会自动重新连接，客户端可不做处理
  LOG_ERROR(name() << ": Disconnected, reason=" << reason);
  connected_ = 0;
}

// 当客户端发出登录请求之后，该方法会被调用，通知客户端登录是否成功
void Trade::OnRspUserLogin(CThostFtdcRspUserLoginField* rsp_user_login,
                           CThostFtdcRspInfoField* rsp_info, int request_id,
                           bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    // 端登失败，客户端需进行错误处理
    LOG_ERROR(name() << ": Failed to login, errorCode=" << rsp_info->ErrorID
                     << ", errorMsg=" << rsp_info->ErrorMsg
                     << " requestId=" << request_id << ", chain=" << is_last);
    return;
  }

  CThostFtdcQrySettlementInfoField req{};
  STRCPY(req.BrokerID, broker_id_.c_str());
  STRCPY(req.InvestorID, user_id_.c_str());
  auto ret = api_->ReqQrySettlementInfo(&req, ++request_counter_);
  if (ret) {
    LOG_ERROR(name() << ": ReqQrySettlementInfo failed: " << ret);
  }

  connected_ = 1;
  LOG_INFO(name() << ": User logged in");
}

static inline decltype(auto) GetTime(const char* datestr, const char* timestr) {
  int h, m, s;
  if (sscanf(timestr, "%d:%d:%d", &h, &m, &s) != 3)
    return opentrade::NowUtcInMicro();
  auto y = atoi(datestr);
  boost::posix_time::ptime pt(
      boost::gregorian::date(y / 10000, y % 10000 / 100, y % 100),
      boost::posix_time::time_duration(h, m, s));
  tm tmp = boost::posix_time::to_tm(pt);
  return mktime(&tmp) * 1000000l;
}

// 报单录入应答
void Trade::OnRspOrderInsert(CThostFtdcInputOrderField* input_order,
                             CThostFtdcRspInfoField* rsp_info, int request_id,
                             bool is_last) {
  // 输出报单录入结果
  if (rsp_info->ErrorID) {
    auto id = atol(input_order->OrderRef);
    tp_.AddTask([info = *rsp_info, id, this]() {
      of_ << "# <- " << opentrade::GetNowStr() << ' ' << "OrderRef=" << id
          << ' ' << "ErrorId=" << info.ErrorID << ' '
          << "ErrorMsg=" << info.ErrorMsg << std::endl;
    });
    HandleNewRejected(id, rsp_info->ErrorMsg);
  }
  // 通知报单录入完成
}

template <typename T>
static inline std::string GetString(T v) {
  char tmp[sizeof(T) + 1];
  strncpy(tmp, v, sizeof(T));
  tmp[sizeof(T)] = 0;
  return tmp;
}

void Trade::OnRtnTrade(CThostFtdcTradeField* trd) {
  auto id = atol(trd->OrderRef);
  auto tm = GetTime(trd->TradeDate, trd->TradeTime);
  HandleFill(id, trd->Volume, trd->Price, GetString(trd->TradeID), tm);

  tp_.AddTask([copy = *trd, this]() {
    of_ << "# trade <- " << opentrade::GetNowStr() << ' '
        << "BrokerID=" << copy.BrokerID << ' '
        << "InvestorID=" << copy.InvestorID << ' '
        << "InstrumentID=" << copy.InstrumentID << ' '
        << "OrderRef=" << copy.OrderRef << ' ' << "UserID=" << copy.UserID
        << ' ' << "ExchangeID=" << copy.ExchangeID << ' '
        << "TradeID=" << copy.TradeID << ' ' << "Direction=" << copy.Direction
        << ' ' << "OrderSysID=" << copy.OrderSysID << ' '
        << "ParticipantID=" << copy.ParticipantID << ' '
        << "ClientID=" << copy.ClientID << ' '
        << "TradingRole=" << copy.TradingRole << ' '
        << "ExchangeInstID=" << copy.ExchangeInstID << ' '
        << "OffsetFlag=" << copy.OffsetFlag << ' '
        << "HedgeFlag=" << copy.HedgeFlag << ' ' << "Price=" << copy.Price
        << ' ' << "Volume=" << copy.Volume << ' '
        << "TradeDate=" << copy.TradeDate << ' '
        << "TradeTime=" << copy.TradeTime << ' '
        << "TradeType=" << copy.TradeType << ' '
        << "PriceSource=" << copy.PriceSource << ' '
        << "TraderID=" << copy.TraderID << ' '
        << "OrderLocalID=" << copy.OrderLocalID << ' '
        << "ClearingPartID=" << copy.ClearingPartID << ' '
        << "BusinessUnit=" << copy.BusinessUnit << ' '
        << "SequenceNo=" << copy.SequenceNo << ' '
        << "TradingDay=" << copy.TradingDay << ' '
        << "SettlementID=" << copy.SettlementID << ' '
        << "BrokerOrderSeq=" << copy.BrokerOrderSeq << ' '
        << "TradeSource=" << copy.TradeSource << std::endl;
  });
}

///报单回报
void Trade::OnRtnOrder(CThostFtdcOrderField* ord) {
  auto id = atol(ord->OrderRef);
  auto& order = orders_[id];
  if (order.seq_num < 0) {
    order.front_id = ord->FrontID;
    order.session_id = ord->SessionID;
  } else if (ord->SequenceNo <= (int64_t)order.seq_num) {
    // some different message has the same seq_num
    // to-do: need to handle duplicate confirmation ourself
    LOG_DEBUG(name() << ": Low SequenceNo " << ord->SequenceNo << " of state="
                     << ord->OrderStatus << ", expected " << (order.seq_num + 1)
                     << " for OrderRef=" << id << ", continue");
  } else {
    order.seq_num = ord->SequenceNo;
  }

  const char* state = "NA";
  switch (ord->OrderStatus) {
    case THOST_FTDC_OST_AllTraded:
      state = "FILLED";
      break;
    case THOST_FTDC_OST_PartTradedQueueing:
    case THOST_FTDC_OST_PartTradedNotQueueing:
      state = "PARTIALLY_FILLED";
      break;
    case THOST_FTDC_OST_Canceled:
      HandleCanceled(id, id, GetString(ord->StatusMsg));
      state = "CANCELED";
      break;
    case THOST_FTDC_OST_NoTradeNotQueueing:
    case THOST_FTDC_OST_Unknown:
      state = "PENDING_NEW";
      break;
    case THOST_FTDC_OST_NoTradeQueueing:
      auto tm = GetTime(ord->InsertDate, ord->InsertTime);
      HandleNew(id, GetString(ord->OrderSysID), tm);
      state = "NEW";
      break;
  }

  tp_.AddTask([copy = *ord, state, id, this]() {
    of_ << id << ' ' << copy.FrontID << ' ' << copy.SessionID << ' '
        << copy.SequenceNo << '\n';
    of_ << "# status <- " << state << ' ' << opentrade::GetNowStr() << ' '
        << "BrokerID=" << copy.BrokerID << ' '
        << "InvestorID=" << copy.InvestorID << ' '
        << "InstrumentID=" << copy.InstrumentID << ' '
        << "OrderRef=" << copy.OrderRef << ' ' << "UserID=" << copy.UserID
        << ' ' << "OrderPriceType=" << copy.OrderPriceType << ' '
        << "Direction=" << copy.Direction << ' '
        << "CombOffsetFlag=" << copy.CombOffsetFlag << ' '
        << "CombHedgeFlag=" << copy.CombHedgeFlag << ' '
        << "LimitPrice=" << copy.LimitPrice << ' '
        << "VolumeTotalOriginal=" << copy.VolumeTotalOriginal << ' '
        << "TimeCondition=" << copy.TimeCondition << ' '
        << "GTDDate=" << copy.GTDDate << ' '
        << "VolumeCondition=" << copy.VolumeCondition << ' '
        << "MinVolume=" << copy.MinVolume << ' '
        << "ContingentCondition=" << copy.ContingentCondition << ' '
        << "StopPrice=" << copy.StopPrice << ' '
        << "ForceCloseReason=" << copy.ForceCloseReason << ' '
        << "IsAutoSuspend=" << copy.IsAutoSuspend << ' '
        << "BusinessUnit=" << copy.BusinessUnit << ' '
        << "RequestID=" << copy.RequestID << ' '
        << "OrderLocalID=" << copy.OrderLocalID << ' '
        << "ExchangeID=" << copy.ExchangeID << ' '
        << "ParticipantID=" << copy.ParticipantID << ' '
        << "ClientID=" << copy.ClientID << ' '
        << "ExchangeInstID=" << copy.ExchangeInstID << ' '
        << "TraderID=" << copy.TraderID << ' ' << "InstallID=" << copy.InstallID
        << ' ' << "OrderSubmitStatus=" << copy.OrderSubmitStatus << ' '
        << "NotifySequence=" << copy.NotifySequence << ' '
        << "TradingDay=" << copy.TradingDay << ' '
        << "SettlementID=" << copy.SettlementID << ' '
        << "OrderSysID=" << copy.OrderSysID << ' '
        << "OrderSource=" << copy.OrderSource << ' '
        << "OrderStatus=" << copy.OrderStatus << ' '
        << "OrderType=" << copy.OrderType << ' '
        << "VolumeTraded=" << copy.VolumeTraded << ' '
        << "VolumeTotal=" << copy.VolumeTotal << ' '
        << "InsertDate=" << copy.InsertDate << ' '
        << "InsertTime=" << copy.InsertTime << ' '
        << "ActiveTime=" << copy.ActiveTime << ' '
        << "SuspendTime=" << copy.SuspendTime << ' '
        << "UpdateTime=" << copy.UpdateTime << ' '
        << "CancelTime=" << copy.CancelTime << ' '
        << "ActiveTraderID=" << copy.ActiveTraderID << ' '
        << "ClearingPartID=" << copy.ClearingPartID << ' '
        << "SequenceNo=" << copy.SequenceNo << ' ' << "FrontID=" << copy.FrontID
        << ' ' << "SessionID=" << copy.SessionID << ' '
        << "UserProductInfo=" << copy.UserProductInfo << ' '
        << "StatusMsg=" << copy.StatusMsg << ' '
        << "UserForceClose=" << copy.UserForceClose << ' '
        << "ActiveUserID=" << copy.ActiveUserID << ' '
        << "BrokerOrderSeq=" << copy.BrokerOrderSeq << ' '
        << "RelativeOrderSysID=" << copy.RelativeOrderSysID << ' '
        << "ZCETotalTradedVolume=" << copy.ZCETotalTradedVolume << ' '
        << "IsSwapOrder=" << copy.IsSwapOrder << std::endl;
  });
}

std::string Trade::Cancel(const opentrade::Order& ord) noexcept {
  auto id = ord.orig_id;
  auto it = orders_.find(id);
  if (it == orders_.end()) {
    return "Can not find original order with front_id and session_id";
  }

  CThostFtdcInputOrderActionField c_ord{};
  //经纪公司代码
  STRCPY(c_ord.BrokerID, broker_id_.c_str());
  //投资者代码
  STRCPY(c_ord.InvestorID, user_id_.c_str());
  // 用户代码
  STRCPY(c_ord.UserID, user_id_.c_str());
  // 合约代码
  STRCPY(c_ord.InstrumentID, ord.sec->local_symbol);
  std::stringstream tmp;
  tmp << std::setfill('0') << std::setw(sizeof(c_ord.OrderRef) - 1) << id;
  strncpy(c_ord.OrderRef, tmp.str().c_str(), sizeof(c_ord.OrderRef) - 1);
  c_ord.FrontID = it->second.front_id;
  c_ord.SessionID = it->second.session_id;

  c_ord.ActionFlag = THOST_FTDC_AF_Delete;
  c_ord.RequestID = ord.id;
  auto ret = api_->ReqOrderAction(&c_ord, ++request_counter_);
  if (ret) {
    LOG_ERROR(name() << ": ReqOrderAction failed: " << ret);
    return "ReqOrderAction failed: " + std::to_string(ret);
  }
  tp_.AddTask([c_ord, this]() {
    of_ << "# Cancel -> " << opentrade::GetNowStr() << ' '
        << "BrokerID=" << c_ord.BrokerID << ' '
        << "InvestorID=" << c_ord.InvestorID << ' '
        << "OrderRef=" << c_ord.OrderRef << ' '
        << "RequestID=" << c_ord.RequestID << ' ' << "FrontID=" << c_ord.FrontID
        << ' ' << "SessionID=" << c_ord.SessionID << ' '
        << "ActionFlag=" << c_ord.ActionFlag << ' ' << "UserID=" << c_ord.UserID
        << ' ' << "InstrumentID=" << c_ord.InstrumentID << std::endl;
  });
  return {};
}

std::string Trade::Place(const opentrade::Order& ord) noexcept {
  // 端登成功,发出报单录入请求
  CThostFtdcInputOrderField c_ord{};
  c_ord.OrderPriceType = THOST_FTDC_OPT_BestPrice;
  switch (ord.type) {
    case opentrade::kLimit:
      c_ord.OrderPriceType = THOST_FTDC_OPT_LimitPrice;
      c_ord.LimitPrice = ord.price;
      break;
    case opentrade::kMarket:
      c_ord.OrderPriceType = THOST_FTDC_OPT_AnyPrice;
      break;
    case opentrade::kStopLimit:
      c_ord.LimitPrice = ord.price;
    case opentrade::kStop:
      // 止损价
      c_ord.StopPrice = ord.stop_price;
      break;
    default:
      break;
  }
  c_ord.Direction = ord.IsBuy() ? THOST_FTDC_D_Buy : THOST_FTDC_D_Sell;
  //经纪公司代码
  STRCPY(c_ord.BrokerID, broker_id_.c_str());
  //投资者代码
  STRCPY(c_ord.InvestorID, user_id_.c_str());
  // 用户代码
  STRCPY(c_ord.UserID, user_id_.c_str());
  // 合约代码
  STRCPY(c_ord.InstrumentID, ord.sec->local_symbol);
  ///报单引用
  std::stringstream tmp;
  tmp << std::setfill('0') << std::setw(sizeof(c_ord.OrderRef) - 1) << ord.id;
  strncpy(c_ord.OrderRef, tmp.str().c_str(), sizeof(c_ord.OrderRef) - 1);
  char offset = 0;
  char hedge = 0;
  if (ord.optional) {
    offset = opentrade::GetParam(*ord.optional, "offset_flag", offset);
    hedge = opentrade::GetParam(*ord.optional, "hedge_flag", hedge);
  }
  // 组合开平标志
  c_ord.CombOffsetFlag[0] = offset;
  // 组合投机套保标志
  c_ord.CombHedgeFlag[0] = hedge;
  // 数量
  c_ord.VolumeTotalOriginal = ord.qty;
  // 有效期类型
  c_ord.TimeCondition = THOST_FTDC_TC_GFD;  ///当日有效
  // GTD日期
  STRCPY(c_ord.GTDDate, "");
  // 成交量类型
  c_ord.VolumeCondition = THOST_FTDC_VC_AV;
  // 最小成交量
  c_ord.MinVolume = 0;
  // 触发条件
  c_ord.ContingentCondition = THOST_FTDC_CC_Immediately;
  // 强平原因
  c_ord.ForceCloseReason = THOST_FTDC_FCC_NotForceClose;
  // 自动挂起标志
  c_ord.IsAutoSuspend = 0;
  c_ord.RequestID = ord.id;
  auto ret = api_->ReqOrderInsert(&c_ord, ++request_counter_);
  if (ret) {
    LOG_ERROR(name() << ": ReqOrderInsert failed: " << ret);
    return "ReqOrderInsert failed: " + std::to_string(ret);
  }
  tp_.AddTask([=]() {
    of_ << "# Place -> " << opentrade::GetNowStr() << ' '
        << "BrokerID=" << c_ord.BrokerID << ' '
        << "InvestorID=" << c_ord.InvestorID << ' '
        << "InstrumentID=" << c_ord.InstrumentID << ' '
        << "OrderRef=" << c_ord.OrderRef << ' ' << "UserID=" << c_ord.UserID
        << ' ' << "OrderPriceType=" << c_ord.OrderPriceType << ' '
        << "Direction=" << c_ord.Direction << ' '
        << "CombOffsetFlag=" << c_ord.CombOffsetFlag << ' '
        << "CombHedgeFlag=" << c_ord.CombHedgeFlag << ' '
        << "LimitPrice=" << c_ord.LimitPrice << ' '
        << "VolumeTotalOriginal=" << c_ord.VolumeTotalOriginal << ' '
        << "TimeCondition=" << c_ord.TimeCondition << ' '
        << "GTDDate=" << c_ord.GTDDate << ' '
        << "VolumeCondition=" << c_ord.VolumeCondition << ' '
        << "MinVolume=" << c_ord.MinVolume << ' '
        << "ContingentCondition=" << c_ord.ContingentCondition << ' '
        << "StopPrice=" << c_ord.StopPrice << ' '
        << "ForceCloseReason=" << c_ord.ForceCloseReason << ' '
        << "IsAutoSuspend=" << c_ord.IsAutoSuspend << ' '
        << "BusinessUnit=" << c_ord.BusinessUnit << ' '
        << "RequestID=" << c_ord.RequestID << ' '
        << "UserForceClose=" << c_ord.UserForceClose << ' '
        << "IsSwapOrder=" << c_ord.IsSwapOrder << std::endl;
  });
  return {};
}

void Trade::OnRspSettlementInfoConfirm(
    CThostFtdcSettlementInfoConfirmField* settlement_info,
    CThostFtdcRspInfoField* rsp_info, int request_id, bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    LOG_ERROR(name() << ": OnRspSettlementInfoConfirm, erroCode = "
                     << rsp_info->ErrorID << " errorMsg = "
                     << rsp_info->ErrorMsg << " requestId = " << request_id
                     << " chain = " << is_last);
    return;
  }
  if (settlement_info) {
    LOG_DEBUG(name() << ": OnRspSettlementInfoConfirm: request_id="
                     << request_id << " is_last=" << is_last
                     << " ConfirmDate=" << settlement_info->ConfirmDate
                     << " ConfirmTime=" << settlement_info->ConfirmTime);
  }
  LOG_INFO(name() << ": Settlement confirmed");
}

void Trade::OnRspQrySettlementInfo(
    CThostFtdcSettlementInfoField* settlement_info,
    CThostFtdcRspInfoField* rsp_info, int request_id, bool is_last) {
  if (rsp_info && rsp_info->ErrorID != 0) {
    LOG_ERROR(
        name() << ": OnRspQrySettlementInfo, erroCode = " << rsp_info->ErrorID
               << " errorMsg = " << rsp_info->ErrorMsg
               << " requestId = " << request_id << " chain = " << is_last);
    return;
  }
  if (settlement_info) {
    LOG_DEBUG(name() << ": OnRspQrySettlementInfo: request_id=" << request_id
                     << " is_last=" << is_last
                     << " TradingDay=" << settlement_info->TradingDay
                     << " SettlementID=" << settlement_info->SettlementID
                     << " Content:\n"
                     << settlement_info->Content);
  }
  if (is_last) {
    CThostFtdcSettlementInfoConfirmField req{};
    STRCPY(req.BrokerID, broker_id_.c_str());
    STRCPY(req.InvestorID, user_id_.c_str());

    auto ret = api_->ReqSettlementInfoConfirm(&req, ++request_counter_);
    if (ret) {
      LOG_ERROR(name() << ": ReqSettlementInfoConfirm failed: " << ret);
    }
    LOG_INFO(name() << ": ReqSettlementInfoConfirm sent");
  }
}

extern "C" {
opentrade::Adapter* create() { return new Trade{}; }
}
