#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <fstream>
#include <iostream>

#include "account.h"
#include "algo.h"
#include "backtest.h"
#include "bar_handler.h"
#include "commission.h"
#include "consolidation.h"
#include "database.h"
#include "exchange_connectivity.h"
#include "logger.h"
#include "market_data.h"
#include "opentick.h"
#include "position.h"
#include "python.h"
#include "risk.h"
#include "security.h"
#include "server.h"
#include "stop_book.h"
#include "test_latency.h"

namespace bpo = boost::program_options;
namespace fs = boost::filesystem;
using opentrade::AlgoManager;
using opentrade::ExchangeConnectivityManager;
using opentrade::kAdapterPrefixes;
using opentrade::kAlgoPath;
using opentrade::kStorePath;
using opentrade::MarketDataManager;
using opentrade::PositionManager;

int main(int argc, char *argv[]) {
  std::string config_file_path;
  std::string log_config_file_path;
  std::string db_url;
  std::string opentick_url;
  uint16_t db_pool_size = 1;
  auto db_create_tables = false;
  auto db_alter_tables = false;
  auto algo_threads = 0;
#ifdef BACKTEST
  std::string backtest_file;
  std::string tick_file;
  auto start_date = 0u;
  auto end_date = 0u;
#else
  auto io_threads = 0;
  auto port = 0;
  auto disable_rms = true;
#endif
  try {
    bpo::options_description config("Configuration");
    config.add_options()("help,h", "produce help message")
#ifdef BACKTEST
        ("backtest_file,b",
         bpo::value<std::string>(&backtest_file)
             ->default_value("./backtest.py"),
         "python file which provides callback functions")(
            "tick_file,t",
            bpo::value<std::string>(&tick_file)
                ->default_value("./ticks/%Y%m%d"),
            "in strftime format, e.g. /to/path/%Y%m%d")(
            "start_date,s", bpo::value<uint32_t>(&start_date),
            "start date, in 'YYYYmmdd' format")(
            "end_date,e", bpo::value<uint32_t>(&end_date),
            "end date, in 'YYYYmmdd' format")
#else
        ("db_create_tables",
         bpo::value<bool>(&db_create_tables)->default_value(false),
         "create database tables")(
            "db_alter_tables",
            bpo::value<bool>(&db_alter_tables)->default_value(false),
            "alter database tables")(
            "db_pool_size",
            bpo::value<uint16_t>(&db_pool_size)->default_value(4),
            "database connection pool size")(
            "port", bpo::value<int>(&port)->default_value(9111), "listen port")(
            "io_threads", bpo::value<int>(&io_threads)->default_value(1),
            "number of web server io threads")(
            "algo_threads", bpo::value<int>(&algo_threads)->default_value(1),
            "number of algo threads")(
            "disable_rms", bpo::value<bool>(&disable_rms)->default_value(false),
            "whether disable rms")
#endif
            ("config_file,c",
             bpo::value<std::string>(&config_file_path)
                 ->default_value("opentrade.conf"),
             "config file path")("log_config_file,l",
                                 bpo::value<std::string>(&log_config_file_path)
                                     ->default_value("log.conf"),
                                 "log4cxx config file path")(
                "db_url", bpo::value<std::string>(&db_url),
                "database connection url")(
                "opentick", bpo::value<std::string>(&opentick_url),
                "opentick connection url");

    bpo::options_description config_file_options;
    config_file_options.add(config);
    bpo::variables_map vm;

    bpo::store(bpo::parse_command_line(argc, argv, config), vm);
    bpo::notify(vm);
    if (vm.count("help")) {
      std::cerr << config << std::endl;
      return 1;
    }

    if (!fs::exists(kStorePath)) {
      fs::create_directory(kStorePath);
    }

    std::ifstream ifs(config_file_path.c_str());
    if (ifs) {
      bpo::store(parse_config_file(ifs, config_file_options, true), vm);
    } else {
      std::cerr << config_file_path << " not found" << std::endl;
      return 1;
    }

    bpo::store(bpo::parse_command_line(argc, argv, config), vm);
    bpo::notify(vm);  // make command line option higher priority
  } catch (bpo::error &e) {
    std::cerr << "Bad Options: " << e.what() << std::endl;
    return 1;
  }

  if (!std::ifstream(log_config_file_path.c_str()).good()) {
    std::ofstream(log_config_file_path)
        .write(opentrade::kDefaultLogConf, strlen(opentrade::kDefaultLogConf));
  }

  opentrade::Logger::Initialize("opentrade", log_config_file_path);

  if (db_url.empty()) {
    LOG_ERROR("db_url not configured");
    return 1;
  }
  opentrade::Database::Initialize(db_url, db_pool_size, db_create_tables,
                                  db_alter_tables);
  opentrade::SecurityManager::Initialize();

#ifdef BACKTEST
  if (backtest_file.empty()) {
    LOG_FATAL("backtest file is not given");
    return -1;
  }
  if (!fs::exists(backtest_file)) {
    LOG_FATAL("backtest file '" << backtest_file << "' does not exist");
  }
  if (end_date < start_date) {
    LOG_FATAL("end_date < start_date");
  }
  if (start_date < 19000000) {
    LOG_FATAL("Invalid start_date " << start_date);
  }
#else
  if (!fs::exists(kAlgoPath)) {
    fs::create_directory(kAlgoPath);
  }

  boost::property_tree::ptree prop_tree;
  boost::property_tree::ini_parser::read_ini(config_file_path, prop_tree);
  for (auto &section : prop_tree) {
    if (!section.second.size()) continue;
    auto section_name = section.first;
    if (section_name.empty()) continue;
    opentrade::Adapter::StrMap params;
    for (auto &item : section.second) {
      auto name = item.first;
      boost::to_lower(name);
      params[name] = item.second.data();
    }
    auto sofile = params["sofile"];
    if (sofile.empty()) continue;
    params.erase("sofile");
    auto adapter = opentrade::Adapter::Load(sofile);
    if (!adapter) continue;
    adapter->set_name(section_name);
    adapter->set_config(params);
    if (adapter->GetVersion() != opentrade::kApiVersion) {
      LOG_ERROR("Version mismatch, "
                << "got " << adapter->GetVersion() << ", expect "
                << opentrade::kApiVersion);
      continue;
    }
    if (section_name.find(kAdapterPrefixes[opentrade::kMdPrefix]) == 0) {
      auto md = dynamic_cast<opentrade::MarketDataAdapter *>(adapter);
      if (!md) LOG_FATAL("Failed to load MarketDataAdapter " << section_name);
      MarketDataManager::Instance().AddAdapter(md);
    } else if (section_name.find(kAdapterPrefixes[opentrade::kEcPrefix]) == 0) {
      auto ec = dynamic_cast<opentrade::ExchangeConnectivityAdapter *>(adapter);
      if (!ec)
        LOG_FATAL("Failed to load ExchangeConnectivityAdapter "
                  << section_name);
      ExchangeConnectivityManager::Instance().AddAdapter(ec);
    } else if (section_name.find(kAdapterPrefixes[opentrade::kCmPrefix]) == 0) {
      auto cm = dynamic_cast<opentrade::CommissionAdapter *>(adapter);
      if (!cm) LOG_FATAL("Failed to load CommissionAdapter " << section_name);
      opentrade::CommissionManager::Instance().AddAdapter(cm);
    } else {
      auto algo = dynamic_cast<opentrade::Algo *>(adapter);
      if (!algo) LOG_FATAL("Failed to load Algo " << section_name);
      AlgoManager::Instance().AddAdapter(algo);
    }
  }

#ifdef TEST_LATENCY
  ExchangeConnectivityManager::Instance().AddAdapter(
      new opentrade::TestLatencyEc);
  MarketDataManager::Instance().AddAdapter<opentrade::TestLatencyMd>();
  AlgoManager::Instance().AddAdapter<opentrade::TestlatencyAlgo>();
#endif

  AlgoManager::Initialize();
  opentrade::AccountManager::Initialize();
  opentrade::StopBookManager::Initialize();
  PositionManager::Initialize();
  opentrade::GlobalOrderBook::Initialize();

  if (disable_rms) {
    LOG_INFO("rms disabled");
    opentrade::RiskManager::Instance().Disable();
  }
#endif

  opentrade::InitalizePy();
  LOG_INFO("Loading python algos from " << kAlgoPath);
  if (fs::is_directory(kAlgoPath)) {
    for (auto &entry :
         boost::make_iterator_range(fs::directory_iterator(kAlgoPath), {})) {
      auto path = entry.path();
      auto fn = path.filename().string();
      if (fn[0] == '.') continue;
      opentrade::Algo *algo = nullptr;
      auto algoname = fn.substr(0, fn.length() - 3);
      if (path.extension() == ".py") {
        algo = opentrade::Python::Load(algoname);
      } else if (path.extension() == ".so") {
        auto adapter = opentrade::Adapter::Load(path.string());
        if (adapter->GetVersion() != opentrade::kApiVersion) {
          LOG_ERROR("Version mismatch, "
                    << "got " << adapter->GetVersion() << ", expect "
                    << opentrade::kApiVersion);
          adapter = nullptr;
        }
        algo = dynamic_cast<opentrade::Algo *>(adapter);
      } else {
        continue;
      }
      if (algo) {
        algo->set_name(algoname);
        AlgoManager::Instance().AddAdapter(algo);
      } else {
        LOG_ERROR("Failed to load algo file " << path);
      }
    }
  }

  if (opentick_url.size())
    opentrade::OpenTick::Instance().Initialize(opentick_url);

  AlgoManager::Instance().AddAdapter<opentrade::BarHandler<>>();
  AlgoManager::Instance().AddAdapter<opentrade::ConsolidationHandler>();

  for (auto &p : MarketDataManager::Instance().adapters()) {
    p.second->Start();
  }
  for (auto &p : ExchangeConnectivityManager::Instance().adapters()) {
    p.second->Start();
  }
  for (auto &p : AlgoManager::Instance().adapters()) {
    p.second->Start();
  }

  AlgoManager::Instance().Run(algo_threads);

#ifdef BACKTEST
  auto &bt = opentrade::Backtest::Instance();
  bt.Start(backtest_file, tick_file);
  boost::gregorian::date dt(start_date / 10000, start_date % 10000 / 100,
                            start_date % 100);
  boost::gregorian::date end(end_date / 10000, end_date % 10000 / 100,
                             end_date % 100);
  while (dt <= end) {
    bt.Play(dt);
    dt += boost::gregorian::date_duration(1);
  }
  bt.End();
#else
  if (!MarketDataManager::Instance().GetDefault()) {
    LOG_FATAL("At least one market data adapter required");
    return -1;
  }
  // wait for some time to get last price updated
  // to-do: update last price from opentick
  auto wait = getenv("UPDATE_PNL_WAIT");
  opentrade::kTimerTaskPool.AddTask(
      []() { PositionManager::Instance().UpdatePnl(); },
      boost::posix_time::seconds(wait ? atoi(wait) : 15));
  opentrade::Server::Start(port, io_threads);
#endif

  return 0;
}
