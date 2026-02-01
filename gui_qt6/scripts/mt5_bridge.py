#!/usr/bin/env python3
"""
MT5 Bridge - Python script for MetaTrader 5 API integration
Communicates with Qt6 GUI via JSON messages over stdin/stdout
"""

import sys
import json
import argparse
from datetime import datetime, timedelta
from pathlib import Path

try:
    import MetaTrader5 as mt5
    import pandas as pd
    import numpy as np
    MT5_AVAILABLE = True
except ImportError:
    MT5_AVAILABLE = False


class MT5Bridge:
    def __init__(self, terminal_path=None):
        self.terminal_path = terminal_path
        self.connected = False
        self.download_cancelled = False

    def send_response(self, data):
        """Send JSON response to stdout"""
        print(json.dumps(data), flush=True)

    def send_error(self, message):
        """Send error response"""
        self.send_response({
            "type": "error",
            "success": False,
            "message": message
        })

    def connect(self):
        """Initialize MT5 connection"""
        if not MT5_AVAILABLE:
            self.send_response({
                "type": "connect",
                "success": False,
                "error": "MetaTrader5 module not installed"
            })
            return

        kwargs = {}
        if self.terminal_path:
            kwargs["path"] = self.terminal_path

        if not mt5.initialize(**kwargs):
            error = mt5.last_error()
            self.send_response({
                "type": "connect",
                "success": False,
                "error": f"Failed to initialize MT5: {error}"
            })
            return

        self.connected = True

        # Get account info
        account_info = mt5.account_info()
        terminal_info = mt5.terminal_info()

        self.send_response({
            "type": "connect",
            "success": True,
            "account": {
                "login": str(account_info.login) if account_info else "",
                "server": account_info.server if account_info else "",
                "company": terminal_info.company if terminal_info else "",
                "name": account_info.name if account_info else "",
                "balance": account_info.balance if account_info else 0,
                "equity": account_info.equity if account_info else 0,
                "margin": account_info.margin if account_info else 0,
                "free_margin": account_info.margin_free if account_info else 0,
                "leverage": account_info.leverage if account_info else 0,
                "currency": account_info.currency if account_info else "USD",
                "trade_allowed": account_info.trade_allowed if account_info else False
            }
        })

    def disconnect(self):
        """Shutdown MT5 connection"""
        if self.connected:
            mt5.shutdown()
            self.connected = False
        self.send_response({
            "type": "disconnect",
            "success": True
        })

    def get_symbols(self):
        """Get list of available symbols"""
        if not self.connected:
            self.send_error("Not connected to MT5")
            return

        symbols = mt5.symbols_get()
        if symbols is None:
            self.send_error(f"Failed to get symbols: {mt5.last_error()}")
            return

        symbol_names = [s.name for s in symbols if s.visible]

        self.send_response({
            "type": "symbols",
            "success": True,
            "symbols": symbol_names
        })

    def get_symbol_info(self, symbol):
        """Get detailed symbol information"""
        if not self.connected:
            self.send_error("Not connected to MT5")
            return

        info = mt5.symbol_info(symbol)
        if info is None:
            self.send_error(f"Symbol {symbol} not found")
            return

        self.send_response({
            "type": "symbol_info",
            "success": True,
            "info": {
                "name": info.name,
                "description": info.description,
                "path": info.path,
                "base_currency": info.currency_base,
                "profit_currency": info.currency_profit,
                "margin_currency": info.currency_margin,
                "bid": info.bid,
                "ask": info.ask,
                "point": info.point,
                "digits": info.digits,
                "tick_size": info.trade_tick_size,
                "tick_value": info.trade_tick_value,
                "contract_size": info.trade_contract_size,
                "volume_min": info.volume_min,
                "volume_max": info.volume_max,
                "volume_step": info.volume_step,
                "swap_long": info.swap_long,
                "swap_short": info.swap_short,
                "swap_mode": info.swap_mode,
                "swap_3days": info.swap_rollover3days,
                "margin_initial": info.margin_initial,
                "margin_maintenance": info.margin_maintenance,
                "visible": info.visible,
                "tradeable": info.trade_mode > 0
            }
        })

    def download_ticks(self, symbol, from_date, to_date, output_path):
        """Download tick data for a symbol"""
        if not self.connected:
            self.send_error("Not connected to MT5")
            return

        self.download_cancelled = False

        # Parse dates
        try:
            start = datetime.fromisoformat(from_date.replace('Z', '+00:00'))
            end = datetime.fromisoformat(to_date.replace('Z', '+00:00'))
        except ValueError as e:
            self.send_error(f"Invalid date format: {e}")
            return

        # Get symbol info for formatting
        info = mt5.symbol_info(symbol)
        if info is None:
            self.send_error(f"Symbol {symbol} not found")
            return

        all_ticks = []
        current_start = start
        total_days = (end - start).days
        processed_days = 0

        # Download in weekly chunks
        while current_start < end and not self.download_cancelled:
            current_end = min(current_start + timedelta(days=7), end)

            ticks = mt5.copy_ticks_range(
                symbol,
                current_start,
                current_end,
                mt5.COPY_TICKS_ALL
            )

            if ticks is not None and len(ticks) > 0:
                all_ticks.append(ticks)

            processed_days += (current_end - current_start).days
            percent = int(100 * processed_days / total_days) if total_days > 0 else 100

            # Send progress
            self.send_response({
                "type": "tick_progress",
                "symbol": symbol,
                "percent": percent,
                "ticks_downloaded": sum(len(t) for t in all_ticks)
            })

            current_start = current_end + timedelta(seconds=1)

        if self.download_cancelled:
            self.send_response({
                "type": "tick_error",
                "symbol": symbol,
                "error": "Download cancelled"
            })
            return

        if not all_ticks:
            self.send_response({
                "type": "tick_error",
                "symbol": symbol,
                "error": "No tick data available for the specified period"
            })
            return

        # Combine and convert to DataFrame
        ticks_combined = np.concatenate(all_ticks)
        df = pd.DataFrame(ticks_combined)

        # Convert time
        df['time'] = pd.to_datetime(df['time'], unit='s')
        df['time_str'] = df['time'].dt.strftime('%Y.%m.%d %H:%M:%S')

        # Add milliseconds
        if 'time_msc' in df.columns:
            df['ms'] = df['time_msc'] % 1000
            df['time_str'] = df['time_str'] + '.' + df['ms'].astype(str).str.zfill(3)

        # Format output (MT5 CSV format)
        output_df = pd.DataFrame({
            'Time': df['time_str'],
            'Bid': df['bid'],
            'Ask': df['ask'],
            'Last': df['last'] if 'last' in df.columns else 0.0,
            'Volume': df['volume'] if 'volume' in df.columns else 0,
            'Flags': df['flags'] if 'flags' in df.columns else 0
        })

        # Save to file
        output_file = Path(output_path)
        output_file.parent.mkdir(parents=True, exist_ok=True)
        output_df.to_csv(output_file, sep='\t', index=False)

        self.send_response({
            "type": "tick_complete",
            "symbol": symbol,
            "path": str(output_file),
            "total_ticks": len(output_df)
        })

    def cancel_download(self):
        """Cancel ongoing download"""
        self.download_cancelled = True
        self.send_response({
            "type": "cancel_download",
            "success": True
        })

    def process_command(self, command_data):
        """Process a command from the GUI"""
        try:
            cmd = command_data.get("command", "")
            params = command_data.get("params", {})

            if cmd == "connect":
                self.connect()
            elif cmd == "disconnect":
                self.disconnect()
            elif cmd == "get_symbols":
                self.get_symbols()
            elif cmd == "get_symbol_info":
                self.get_symbol_info(params.get("symbol", ""))
            elif cmd == "download_ticks":
                self.download_ticks(
                    params.get("symbol", ""),
                    params.get("from_date", ""),
                    params.get("to_date", ""),
                    params.get("output_path", "")
                )
            elif cmd == "cancel_download":
                self.cancel_download()
            else:
                self.send_error(f"Unknown command: {cmd}")

        except Exception as e:
            self.send_error(f"Error processing command: {str(e)}")

    def run(self):
        """Main loop - read commands from stdin"""
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue

            try:
                command_data = json.loads(line)
                self.process_command(command_data)
            except json.JSONDecodeError as e:
                self.send_error(f"Invalid JSON: {e}")
            except Exception as e:
                self.send_error(f"Error: {e}")


def main():
    parser = argparse.ArgumentParser(description="MT5 Bridge for Qt6 GUI")
    parser.add_argument("--terminal", help="Path to MT5 terminal")
    args = parser.parse_args()

    bridge = MT5Bridge(terminal_path=args.terminal)
    bridge.run()


if __name__ == "__main__":
    main()
