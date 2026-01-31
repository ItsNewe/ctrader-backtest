"""
MT5 Bridge Server

Provides a socket-based interface for C++ to interact with MT5.
Runs as a persistent process that handles trading commands.

Usage:
    python mt5_bridge.py --port 5555 --account <account_number>

Requirements:
    pip install MetaTrader5 pyzmq

Architecture:
    C++ Client <--ZMQ--> Python Bridge <--API--> MT5 Terminal
"""

import sys
import json
import time
import argparse
import threading
from datetime import datetime
from typing import Optional, Dict, Any, List

try:
    import MetaTrader5 as mt5
    HAS_MT5 = True
except ImportError:
    HAS_MT5 = False
    print("Warning: MetaTrader5 package not installed. Run: pip install MetaTrader5")

try:
    import zmq
    HAS_ZMQ = True
except ImportError:
    HAS_ZMQ = False
    print("Warning: pyzmq package not installed. Run: pip install pyzmq")


class MT5Bridge:
    """Bridge between ZMQ socket and MT5 API."""

    def __init__(self, account: int = 0, password: str = "", server: str = ""):
        self.account = account
        self.password = password
        self.server = server
        self.connected = False
        self.magic_number = 123456  # EA identifier

    def connect(self) -> bool:
        """Connect to MT5 terminal."""
        if not HAS_MT5:
            return False

        if not mt5.initialize():
            print(f"MT5 initialize failed: {mt5.last_error()}")
            return False

        if self.account > 0:
            if not mt5.login(self.account, password=self.password, server=self.server):
                print(f"MT5 login failed: {mt5.last_error()}")
                return False

        self.connected = True
        account_info = mt5.account_info()
        if account_info:
            print(f"Connected to MT5: {account_info.login} @ {account_info.server}")
            print(f"Balance: {account_info.balance} {account_info.currency}")
        return True

    def disconnect(self):
        """Disconnect from MT5."""
        if HAS_MT5:
            mt5.shutdown()
        self.connected = False

    def get_tick(self, symbol: str) -> Dict[str, Any]:
        """Get current tick for symbol."""
        if not self.connected:
            return {"error": "Not connected"}

        tick = mt5.symbol_info_tick(symbol)
        if tick is None:
            return {"error": f"Failed to get tick for {symbol}"}

        return {
            "symbol": symbol,
            "bid": tick.bid,
            "ask": tick.ask,
            "time": datetime.fromtimestamp(tick.time).isoformat(),
            "volume": tick.volume
        }

    def get_account_info(self) -> Dict[str, Any]:
        """Get account information."""
        if not self.connected:
            return {"error": "Not connected"}

        info = mt5.account_info()
        if info is None:
            return {"error": "Failed to get account info"}

        return {
            "balance": info.balance,
            "equity": info.equity,
            "margin": info.margin,
            "free_margin": info.margin_free,
            "margin_level": info.margin_level,
            "currency": info.currency,
            "leverage": info.leverage,
            "profit": info.profit
        }

    def get_positions(self, symbol: str = "") -> List[Dict[str, Any]]:
        """Get open positions."""
        if not self.connected:
            return []

        if symbol:
            positions = mt5.positions_get(symbol=symbol)
        else:
            positions = mt5.positions_get()

        if positions is None:
            return []

        result = []
        for pos in positions:
            result.append({
                "ticket": pos.ticket,
                "symbol": pos.symbol,
                "is_buy": pos.type == mt5.ORDER_TYPE_BUY,
                "lots": pos.volume,
                "open_price": pos.price_open,
                "stop_loss": pos.sl,
                "take_profit": pos.tp,
                "current_profit": pos.profit,
                "open_time": datetime.fromtimestamp(pos.time).isoformat(),
                "comment": pos.comment,
                "magic": pos.magic
            })

        return result

    def send_order(self, request: Dict[str, Any]) -> Dict[str, Any]:
        """Send trading order."""
        if not self.connected:
            return {"success": False, "error": "Not connected"}

        symbol = request.get("symbol", "")
        order_type = request.get("type", "")
        lots = request.get("lots", 0.01)
        price = request.get("price", 0)
        sl = request.get("stop_loss", 0)
        tp = request.get("take_profit", 0)
        comment = request.get("comment", "")

        # Get symbol info
        symbol_info = mt5.symbol_info(symbol)
        if symbol_info is None:
            return {"success": False, "error": f"Symbol {symbol} not found"}

        if not symbol_info.visible:
            if not mt5.symbol_select(symbol, True):
                return {"success": False, "error": f"Failed to select symbol {symbol}"}

        # Determine order type
        if order_type == "MARKET_BUY":
            mt5_type = mt5.ORDER_TYPE_BUY
            price = mt5.symbol_info_tick(symbol).ask
        elif order_type == "MARKET_SELL":
            mt5_type = mt5.ORDER_TYPE_SELL
            price = mt5.symbol_info_tick(symbol).bid
        else:
            return {"success": False, "error": f"Unknown order type: {order_type}"}

        # Build order request
        mt5_request = {
            "action": mt5.TRADE_ACTION_DEAL,
            "symbol": symbol,
            "volume": lots,
            "type": mt5_type,
            "price": price,
            "sl": sl,
            "tp": tp,
            "deviation": 20,  # Max slippage in points
            "magic": self.magic_number,
            "comment": comment,
            "type_time": mt5.ORDER_TIME_GTC,
            "type_filling": mt5.ORDER_FILLING_IOC,
        }

        # Send order
        result = mt5.order_send(mt5_request)
        if result is None:
            return {"success": False, "error": f"Order send failed: {mt5.last_error()}"}

        if result.retcode != mt5.TRADE_RETCODE_DONE:
            return {
                "success": False,
                "error": f"Order rejected: {result.comment}",
                "retcode": result.retcode
            }

        return {
            "success": True,
            "ticket": result.order,
            "filled_price": result.price,
            "filled_lots": result.volume
        }

    def close_position(self, ticket: int, lots: float = 0) -> Dict[str, Any]:
        """Close a position."""
        if not self.connected:
            return {"success": False, "error": "Not connected"}

        # Get position
        positions = mt5.positions_get(ticket=ticket)
        if positions is None or len(positions) == 0:
            return {"success": False, "error": f"Position {ticket} not found"}

        position = positions[0]
        symbol = position.symbol
        close_lots = lots if lots > 0 else position.volume

        # Determine close type (opposite of position type)
        if position.type == mt5.ORDER_TYPE_BUY:
            close_type = mt5.ORDER_TYPE_SELL
            price = mt5.symbol_info_tick(symbol).bid
        else:
            close_type = mt5.ORDER_TYPE_BUY
            price = mt5.symbol_info_tick(symbol).ask

        request = {
            "action": mt5.TRADE_ACTION_DEAL,
            "symbol": symbol,
            "volume": close_lots,
            "type": close_type,
            "position": ticket,
            "price": price,
            "deviation": 20,
            "magic": self.magic_number,
            "comment": "close",
            "type_time": mt5.ORDER_TIME_GTC,
            "type_filling": mt5.ORDER_FILLING_IOC,
        }

        result = mt5.order_send(request)
        if result is None:
            return {"success": False, "error": f"Close failed: {mt5.last_error()}"}

        if result.retcode != mt5.TRADE_RETCODE_DONE:
            return {
                "success": False,
                "error": f"Close rejected: {result.comment}",
                "retcode": result.retcode
            }

        return {
            "success": True,
            "closed_price": result.price,
            "closed_lots": result.volume
        }

    def modify_position(self, ticket: int, sl: float, tp: float) -> Dict[str, Any]:
        """Modify position SL/TP."""
        if not self.connected:
            return {"success": False, "error": "Not connected"}

        positions = mt5.positions_get(ticket=ticket)
        if positions is None or len(positions) == 0:
            return {"success": False, "error": f"Position {ticket} not found"}

        position = positions[0]

        request = {
            "action": mt5.TRADE_ACTION_SLTP,
            "symbol": position.symbol,
            "position": ticket,
            "sl": sl,
            "tp": tp,
        }

        result = mt5.order_send(request)
        if result is None:
            return {"success": False, "error": f"Modify failed: {mt5.last_error()}"}

        if result.retcode != mt5.TRADE_RETCODE_DONE:
            return {
                "success": False,
                "error": f"Modify rejected: {result.comment}",
                "retcode": result.retcode
            }

        return {"success": True}

    def close_all_positions(self, symbol: str = "") -> Dict[str, Any]:
        """Close all positions."""
        positions = self.get_positions(symbol)
        closed = 0
        errors = []

        for pos in positions:
            result = self.close_position(pos["ticket"])
            if result["success"]:
                closed += 1
            else:
                errors.append(f"Ticket {pos['ticket']}: {result.get('error', 'Unknown error')}")

        return {
            "success": len(errors) == 0,
            "closed": closed,
            "errors": errors
        }

    def get_symbol_info(self, symbol: str) -> Dict[str, Any]:
        """Get symbol information."""
        if not self.connected:
            return {"error": "Not connected"}

        info = mt5.symbol_info(symbol)
        if info is None:
            return {"error": f"Symbol {symbol} not found"}

        return {
            "symbol": symbol,
            "point": info.point,
            "digits": info.digits,
            "contract_size": info.trade_contract_size,
            "min_lot": info.volume_min,
            "max_lot": info.volume_max,
            "lot_step": info.volume_step,
            "swap_long": info.swap_long,
            "swap_short": info.swap_short
        }

    def handle_command(self, command: Dict[str, Any]) -> Dict[str, Any]:
        """Handle incoming command from C++ client."""
        cmd = command.get("cmd", "")

        if cmd == "connect":
            success = self.connect()
            return {"success": success}

        elif cmd == "disconnect":
            self.disconnect()
            return {"success": True}

        elif cmd == "get_tick":
            return self.get_tick(command.get("symbol", ""))

        elif cmd == "get_account":
            return self.get_account_info()

        elif cmd == "get_positions":
            return {"positions": self.get_positions(command.get("symbol", ""))}

        elif cmd == "send_order":
            return self.send_order(command)

        elif cmd == "close_position":
            return self.close_position(
                command.get("ticket", 0),
                command.get("lots", 0)
            )

        elif cmd == "modify_position":
            return self.modify_position(
                command.get("ticket", 0),
                command.get("sl", 0),
                command.get("tp", 0)
            )

        elif cmd == "close_all":
            return self.close_all_positions(command.get("symbol", ""))

        elif cmd == "get_symbol_info":
            return self.get_symbol_info(command.get("symbol", ""))

        elif cmd == "ping":
            return {"pong": True, "time": datetime.now().isoformat()}

        else:
            return {"error": f"Unknown command: {cmd}"}


class BridgeServer:
    """ZMQ server for MT5 bridge."""

    def __init__(self, port: int, bridge: MT5Bridge):
        self.port = port
        self.bridge = bridge
        self.running = False

    def start(self):
        """Start the ZMQ server."""
        if not HAS_ZMQ:
            print("Error: pyzmq not installed")
            return

        context = zmq.Context()
        socket = context.socket(zmq.REP)
        socket.bind(f"tcp://*:{self.port}")

        print(f"MT5 Bridge Server started on port {self.port}")
        print("Waiting for commands...")

        self.running = True
        while self.running:
            try:
                # Wait for message
                message = socket.recv_string()
                command = json.loads(message)

                # Handle command
                response = self.bridge.handle_command(command)

                # Send response
                socket.send_string(json.dumps(response))

            except zmq.ZMQError as e:
                if e.errno == zmq.ETERM:
                    break
                print(f"ZMQ error: {e}")
            except json.JSONDecodeError as e:
                socket.send_string(json.dumps({"error": f"Invalid JSON: {e}"}))
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Error: {e}")
                socket.send_string(json.dumps({"error": str(e)}))

        socket.close()
        context.term()
        print("Server stopped")

    def stop(self):
        """Stop the server."""
        self.running = False


def main():
    parser = argparse.ArgumentParser(description="MT5 Bridge Server")
    parser.add_argument("--port", type=int, default=5555, help="ZMQ port")
    parser.add_argument("--account", type=int, default=0, help="MT5 account number")
    parser.add_argument("--password", type=str, default="", help="MT5 password")
    parser.add_argument("--server", type=str, default="", help="MT5 server")

    args = parser.parse_args()

    if not HAS_MT5:
        print("Error: MetaTrader5 package required. Install with: pip install MetaTrader5")
        sys.exit(1)

    if not HAS_ZMQ:
        print("Error: pyzmq package required. Install with: pip install pyzmq")
        sys.exit(1)

    bridge = MT5Bridge(args.account, args.password, args.server)
    server = BridgeServer(args.port, bridge)

    try:
        # Connect to MT5 on startup
        if not bridge.connect():
            print("Warning: Could not connect to MT5. Make sure terminal is running.")

        server.start()
    finally:
        bridge.disconnect()


if __name__ == "__main__":
    main()
