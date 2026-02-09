#ifndef CTRADER_CONNECTOR_H
#define CTRADER_CONNECTOR_H

#include "backtest_engine.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

// ==================== Configuration ====================

namespace ctrader {

struct CTraderConfig {
  std::string client_id;
  std::string client_secret;
  std::string host;
  int port;
  bool use_ssl;
  bool is_demo;

  CTraderConfig()
      : host("demo.ctraderapi.com"), port(5035), use_ssl(true), is_demo(true) {
  }

  static CTraderConfig Demo() {
    CTraderConfig config;
    config.host = "demo.ctraderapi.com";
    config.port = 5035;
    config.is_demo = true;
    return config;
  }

  static CTraderConfig Live() {
    CTraderConfig config;
    config.host = "live.ctraderapi.com";
    config.port = 5035;
    config.is_demo = false;
    return config;
  }
};

// ==================== Message Types ====================

enum class MessageType {
  // Application level
  APPLICATION_AUTH_REQ = 2100,
  APPLICATION_AUTH_RES = 2101,
  ACCOUNT_AUTH_REQ = 2102,
  ACCOUNT_AUTH_RES = 2103,

  // Market data
  SUBSCRIBE_SPOTS_REQ = 2116,
  SUBSCRIBE_SPOTS_RES = 2117,
  SPOT_EVENT = 2118,
  SUBSCRIBE_TRENDBAR_REQ = 2119,
  SUBSCRIBE_TRENDBAR_RES = 2120,
  TRENDBAR_EVENT = 2121,
  GET_TRENDBARS_REQ = 2122,
  GET_TRENDBARS_RES = 2123,
  GET_TICKDATA_REQ = 2124,
  GET_TICKDATA_RES = 2125,

  // Trading
  NEW_ORDER_REQ = 2126,
  NEW_ORDER_RES = 2127,
  CANCEL_ORDER_REQ = 2128,
  CANCEL_ORDER_RES = 2129,
  AMEND_ORDER_REQ = 2130,
  AMEND_ORDER_RES = 2131,
  CLOSE_POSITION_REQ = 2132,
  CLOSE_POSITION_RES = 2133,

  // Account info
  GET_ACCOUNTS_REQ = 2104,
  GET_ACCOUNTS_RES = 2105,
  TRADER_REQ = 2124,
  TRADER_RES = 2125,

  // Events
  EXECUTION_EVENT = 2134,
  ORDER_ERROR_EVENT = 2135,

  // Error
  ERROR_RES = 50,
  HEARTBEAT_EVENT = 51
};

// ==================== Data Structures ====================

struct CTraderTick {
  int64_t timestamp;
  double bid;
  double ask;
  std::string symbol_id;
};

struct CTraderBar {
  int64_t timestamp;
  double open;
  double high;
  double low;
  double close;
  int64_t volume;
  std::string symbol_id;
};

struct CTraderSymbol {
  int64_t symbol_id;
  std::string symbol_name;
  int digits;
  double pip_position;
  bool enabled_for_trading;
  double min_volume;
  double max_volume;
  double volume_step;
};

struct CTraderAccount {
  int64_t account_id;
  std::string login;
  double balance;
  std::string currency;
  bool is_live;
};

struct CTraderOrder {
  int64_t order_id;
  int64_t position_id;
  std::string symbol_name;
  bool is_buy;
  double volume;
  double open_price;
  double stop_loss;
  double take_profit;
  int64_t open_timestamp;
};

// ==================== Connection Handler ====================

class CTraderConnection {
 private:
  std::atomic<int> socket_fd_;
  std::atomic<bool> connected_;
  std::atomic<bool> authenticated_;
  std::mutex send_mutex_;
  std::thread receive_thread_;
  std::atomic<bool> running_;

  std::function<void(const std::vector<uint8_t>&)> message_callback_;

  bool ConnectSocket(const std::string& host, int port) {
    // TODO: Implement actual socket connection
    // You'll need to use:
    // - socket() and connect() on Linux/Mac
    // - Winsock on Windows
    // - OpenSSL for SSL/TLS

    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;

    // Placeholder - implement with actual socket code
    socket_fd_ = -1;  // Would be actual socket descriptor
    connected_ = true;  // Would be set based on actual connection

    return connected_;
  }

  void ReceiveLoop() {
    while (running_) {
      // Read message length (first 4 bytes)
      uint32_t message_length = 0;

      // TODO: Implement actual socket receive
      // recv(socket_fd_, &message_length, 4, 0);

      if (message_length > 0 &&
          message_length < 1024 * 1024) {  // Sanity check
        std::vector<uint8_t> message_data(message_length);

        // TODO: Implement actual socket receive
        // recv(socket_fd_, message_data.data(), message_length, 0);

        if (message_callback_) {
          message_callback_(message_data);
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

 public:
  CTraderConnection()
      : socket_fd_(-1), connected_(false), authenticated_(false),
        running_(false) {
    // Initial state is already handled by std::atomic defaults or this constructor
  }

  ~CTraderConnection() { Disconnect(); }

  bool Connect(const CTraderConfig& config) {
    if (connected_) return true;

    if (!ConnectSocket(config.host, config.port)) {
      return false;
    }

    running_ = true;
    receive_thread_ = std::thread(&CTraderConnection::ReceiveLoop, this);

    return true;
  }

  void Disconnect() {
    running_ = false;

    if (receive_thread_.joinable()) {
      receive_thread_.join();
    }

    if (socket_fd_ >= 0) {
      // TODO: close(socket_fd_); on Linux/Mac
      // closesocket(socket_fd_); on Windows
      socket_fd_ = -1;
    }

    connected_ = false;
    authenticated_ = false;
  }

  bool SendMessage(MessageType type, const std::vector<uint8_t>& payload) {
    if (!connected_) return false;

    std::lock_guard<std::mutex> lock(send_mutex_);

    // Protocol: [4 bytes length][4 bytes type][payload]
    uint32_t total_length = 8 + payload.size();
    uint32_t message_type = static_cast<uint32_t>(type);

    std::vector<uint8_t> message;
    message.resize(total_length + 4);  // +4 for length prefix

    // Write total length
    std::memcpy(message.data(), &total_length, 4);

    // Write message type
    std::memcpy(message.data() + 4, &message_type, 4);

    // Write payload
    if (!payload.empty()) {
      std::memcpy(message.data() + 8, payload.data(), payload.size());
    }

    // TODO: Implement actual socket send
    // send(socket_fd_, message.data(), message.size(), 0);

    return true;
  }

  void SetMessageCallback(
      std::function<void(const std::vector<uint8_t>&)> callback) {
    message_callback_ = callback;
  }

  bool IsConnected() const { return connected_; }
  bool IsAuthenticated() const { return authenticated_; }
  void SetAuthenticated(bool auth) { authenticated_ = auth; }
};

// ==================== cTrader Client ====================

class CTraderClient {
 private:
  CTraderConfig config_;
  CTraderConnection connection_;
  int64_t current_account_id_;
  std::vector<CTraderAccount> accounts_;
  std::vector<CTraderSymbol> symbols_;

  std::function<void(const CTraderTick&)> tick_callback_;
  std::function<void(const CTraderBar&)> bar_callback_;
  std::function<void(const std::string&)> error_callback_;

  void HandleMessage(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return;

    uint32_t message_type;
    std::memcpy(&message_type, data.data(), 4);

    std::vector<uint8_t> payload(data.begin() + 4, data.end());

    switch (static_cast<MessageType>(message_type)) {
      case MessageType::APPLICATION_AUTH_RES:
        HandleApplicationAuthResponse(payload);
        break;
      case MessageType::ACCOUNT_AUTH_RES:
        HandleAccountAuthResponse(payload);
        break;
      case MessageType::SPOT_EVENT:
        HandleSpotEvent(payload);
        break;
      case MessageType::TRENDBAR_EVENT:
        HandleTrendbarEvent(payload);
        break;
      case MessageType::GET_TRENDBARS_RES:
        HandleGetTrendbarsResponse(payload);
        break;
      case MessageType::ERROR_RES:
        HandleErrorResponse(payload);
        break;
      default:
        std::cout << "Unhandled message type: " << message_type << std::endl;
        break;
    }
  }

  void HandleApplicationAuthResponse(const std::vector<uint8_t>& payload) {
    std::cout << "Application authenticated successfully" << std::endl;
    connection_.SetAuthenticated(true);
  }

  void HandleAccountAuthResponse(const std::vector<uint8_t>& payload) {
    std::cout << "Account authenticated successfully" << std::endl;
  }

  void HandleSpotEvent(const std::vector<uint8_t>& payload) {
    if (tick_callback_) {
      CTraderTick tick;
      // Parse from payload
      tick_callback_(tick);
    }
  }

  void HandleTrendbarEvent(const std::vector<uint8_t>& payload) {
    if (bar_callback_) {
      CTraderBar bar;
      // Parse from payload
      bar_callback_(bar);
    }
  }

  void HandleGetTrendbarsResponse(const std::vector<uint8_t>& payload) {
    std::cout << "Received historical trendbars" << std::endl;
  }

  void HandleErrorResponse(const std::vector<uint8_t>& payload) {
    if (error_callback_) {
      error_callback_("API Error occurred");
    }
  }

 public:
  CTraderClient(const CTraderConfig& config)
      : config_(config), current_account_id_(0) {}

  bool Connect() {
    if (!connection_.Connect(config_)) {
      std::cerr << "Failed to connect to cTrader API" << std::endl;
      return false;
    }

    connection_.SetMessageCallback([this](const std::vector<uint8_t>& data) {
      HandleMessage(data);
    });

    return true;
  }

  bool Authenticate() {
    if (!connection_.IsConnected()) {
      std::cerr << "Not connected" << std::endl;
      return false;
    }

    std::vector<uint8_t> auth_payload;
    // TODO: Serialize protobuf message with client_id and client_secret

    return connection_.SendMessage(MessageType::APPLICATION_AUTH_REQ,
                                  auth_payload);
  }

  bool AuthenticateAccount(int64_t account_id) {
    std::vector<uint8_t> payload;
    current_account_id_ = account_id;

    return connection_.SendMessage(MessageType::ACCOUNT_AUTH_REQ, payload);
  }

  bool SubscribeToSpots(const std::vector<std::string>& symbols) {
    std::vector<uint8_t> payload;

    return connection_.SendMessage(MessageType::SUBSCRIBE_SPOTS_REQ, payload);
  }

  bool GetHistoricalBars(const std::string& symbol, int64_t from_timestamp,
                        int64_t to_timestamp, int timeframe) {
    std::vector<uint8_t> payload;

    return connection_.SendMessage(MessageType::GET_TRENDBARS_REQ, payload);
  }

  bool GetHistoricalTicks(const std::string& symbol, int64_t from_timestamp,
                         int64_t to_timestamp) {
    std::vector<uint8_t> payload;

    return connection_.SendMessage(MessageType::GET_TICKDATA_REQ, payload);
  }

  bool PlaceMarketOrder(const std::string& symbol, bool is_buy, double volume,
                       double stop_loss = 0, double take_profit = 0) {
    std::vector<uint8_t> payload;

    return connection_.SendMessage(MessageType::NEW_ORDER_REQ, payload);
  }

  bool ClosePosition(int64_t position_id, double volume) {
    std::vector<uint8_t> payload;

    return connection_.SendMessage(MessageType::CLOSE_POSITION_REQ, payload);
  }

  void SetTickCallback(std::function<void(const CTraderTick&)> callback) {
    tick_callback_ = callback;
  }

  void SetBarCallback(std::function<void(const CTraderBar&)> callback) {
    bar_callback_ = callback;
  }

  void SetErrorCallback(std::function<void(const std::string&)> callback) {
    error_callback_ = callback;
  }

  void Disconnect() { connection_.Disconnect(); }
};

// ==================== Integration with Backtesting Engine ====================

class CTraderDataFeed {
 private:
  CTraderClient client_;
  bool is_downloading_;
  std::vector<backtest::Bar> downloaded_bars_;
  std::vector<backtest::Tick> downloaded_ticks_;
  std::mutex data_mutex_;

 public:
  CTraderDataFeed(const CTraderConfig& config)
      : client_(config), is_downloading_(false) {}

  bool Connect() {
    if (!client_.Connect()) {
      return false;
    }

    std::cout << "Connected to cTrader API" << std::endl;

    // Wait a bit for connection to stabilize
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (!client_.Authenticate()) {
      std::cerr << "Authentication failed" << std::endl;
      return false;
    }

    // Wait for authentication
    std::this_thread::sleep_for(std::chrono::seconds(2));

    return true;
  }

  std::vector<backtest::Bar> DownloadHistoricalBars(
      const std::string& symbol, int64_t from_timestamp, int64_t to_timestamp,
      int timeframe) {
    is_downloading_ = true;
    downloaded_bars_.clear();

    // Set callback to collect bars
    client_.SetBarCallback([this](const CTraderBar& bar) {
      std::lock_guard<std::mutex> lock(data_mutex_);

      backtest::Bar engine_bar;
      engine_bar.time = bar.timestamp / 1000;  // Convert from ms
      engine_bar.open = bar.open;
      engine_bar.high = bar.high;
      engine_bar.low = bar.low;
      engine_bar.close = bar.close;
      engine_bar.volume = bar.volume;

      downloaded_bars_.push_back(engine_bar);
    });

    // Request historical data
    client_.GetHistoricalBars(symbol, from_timestamp, to_timestamp, timeframe);

    // Wait for download (in real implementation, use condition variable)
    std::this_thread::sleep_for(std::chrono::seconds(5));

    is_downloading_ = false;

    std::cout << "Downloaded " << downloaded_bars_.size() << " bars"
              << std::endl;
    return downloaded_bars_;
  }

  std::vector<backtest::Tick> DownloadHistoricalTicks(
      const std::string& symbol, int64_t from_timestamp,
      int64_t to_timestamp) {
    is_downloading_ = true;
    downloaded_ticks_.clear();

    client_.SetTickCallback([this](const CTraderTick& ctick) {
      std::lock_guard<std::mutex> lock(data_mutex_);

      backtest::Tick engine_tick;
      engine_tick.time = ctick.timestamp / 1000;
      engine_tick.time_msc = ctick.timestamp;
      engine_tick.bid = ctick.bid;
      engine_tick.ask = ctick.ask;
      engine_tick.last = (ctick.bid + ctick.ask) / 2.0;

      downloaded_ticks_.push_back(engine_tick);
    });

    client_.GetHistoricalTicks(symbol, from_timestamp, to_timestamp);

    // Wait for download
    std::this_thread::sleep_for(std::chrono::seconds(10));

    is_downloading_ = false;

    std::cout << "Downloaded " << downloaded_ticks_.size() << " ticks"
              << std::endl;
    return downloaded_ticks_;
  }

  void Disconnect() { client_.Disconnect(); }
};

}  // namespace ctrader

#endif  // CTRADER_CONNECTOR_H
