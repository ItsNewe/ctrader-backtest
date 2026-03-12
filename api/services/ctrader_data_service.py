"""
cTrader Open API tick data download service.
Downloads historical tick data via the cTrader Open API protobuf protocol.
Uses raw TLS sockets (no Twisted reactor) for clean sync/async integration.
"""

import asyncio
import logging
import os
import ssl
import socket
import struct
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional, Callable

logger = logging.getLogger(__name__)

_project_root = Path(__file__).parent.parent.parent

# cTrader Open API protobuf payload type constants
_PT_HEARTBEAT = 51
_PT_APP_AUTH_REQ = 2100
_PT_APP_AUTH_RES = 2101
_PT_ACCOUNT_AUTH_REQ = 2102
_PT_ACCOUNT_AUTH_RES = 2103
_PT_SYMBOLS_LIST_REQ = 2114
_PT_SYMBOLS_LIST_RES = 2115
_PT_ERROR_RES = 2142
_PT_GET_TICKDATA_REQ = 2145
_PT_GET_TICKDATA_RES = 2146

DEMO_HOST = "demo.ctraderapi.com"
LIVE_HOST = "live.ctraderapi.com"
PROTO_PORT = 5035


class _CTraderConnection:
    """Synchronous protobuf-over-TLS client for cTrader Open API.

    Implements the wire protocol directly (4-byte big-endian length prefix +
    serialized ProtoMessage wrapper) to avoid Twisted reactor lifecycle issues.
    """

    def __init__(self, host: str, port: int = PROTO_PORT):
        ctx = ssl.create_default_context()
        raw_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        raw_sock.settimeout(30)
        self.sock = ctx.wrap_socket(raw_sock, server_hostname=host)
        self.sock.connect((host, port))

    def send(self, payload_type: int, message) -> None:
        """Send a protobuf message wrapped in ProtoMessage."""
        from ctrader_open_api.messages.OpenApiCommonMessages_pb2 import ProtoMessage
        wrapper = ProtoMessage()
        wrapper.payloadType = payload_type
        wrapper.payload = message.SerializeToString()
        data = wrapper.SerializeToString()
        self.sock.sendall(struct.pack(">I", len(data)) + data)

    def recv(self, timeout: float = 30.0):
        """Receive next message, skipping heartbeats.

        Returns (payload_type, raw_payload_bytes).
        """
        from ctrader_open_api.messages.OpenApiCommonMessages_pb2 import ProtoMessage
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            self.sock.settimeout(max(remaining, 1.0))

            length_bytes = self._recv_exact(4)
            length = struct.unpack(">I", length_bytes)[0]
            data = self._recv_exact(length)

            wrapper = ProtoMessage()
            wrapper.ParseFromString(data)

            if wrapper.payloadType == _PT_HEARTBEAT:
                continue

            return wrapper.payloadType, wrapper.payload

        raise TimeoutError("Timed out waiting for cTrader API response")

    def _recv_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Connection closed by cTrader server")
            buf.extend(chunk)
        return bytes(buf)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def _parse_response(payload_type: int, payload: bytes, expected_type: int, response_class):
    """Parse a protobuf response payload, raising on API errors."""
    if payload_type == _PT_ERROR_RES:
        from ctrader_open_api.messages.OpenApiMessages_pb2 import ProtoOAErrorRes
        error = ProtoOAErrorRes()
        error.ParseFromString(payload)
        raise RuntimeError(f"cTrader API error: {error.errorCode} - {error.description}")

    if payload_type != expected_type:
        raise RuntimeError(f"Unexpected response type {payload_type}, expected {expected_type}")

    msg = response_class()
    msg.ParseFromString(payload)
    return msg


def _decode_tick_data(tick_data_list) -> list:
    """Decode delta-encoded timestamps AND prices.

    cTrader Open API returns ticks newest-first with fields delta-encoded:
    - First tick: absolute timestamp (ms), absolute price (scaled by 100000)
    - Subsequent ticks: signed deltas (negative for timestamps going back in time,
      signed for price changes)

    Returns list of (timestamp_ms, price) tuples in chronological order.
    """
    result = []
    current_ts = 0
    current_price = 0
    for i, td in enumerate(tick_data_list):
        if i == 0:
            current_ts = td.timestamp
            current_price = td.tick
        else:
            # Deltas are signed: timestamps negative (going back), prices +/-
            current_ts += td.timestamp
            current_price += td.tick
        price = current_price / 100000.0
        result.append((current_ts, price))

    # Reverse to chronological order (oldest first)
    result.reverse()
    return result


def _week_chunks(start_dt: datetime, end_dt: datetime) -> list:
    """Split date range into trading-week chunks (Mon-Fri), each <=7 days.

    Skips weekends to avoid empty API requests.
    """
    chunks = []
    current = start_dt
    while current < end_dt:
        weekday = current.weekday()
        # Skip weekends
        if weekday == 5:  # Saturday
            current += timedelta(days=2)
            continue
        elif weekday == 6:  # Sunday
            current += timedelta(days=1)
            continue

        # Chunk from current weekday to Saturday (or end_date, whichever is sooner)
        days_to_saturday = 5 - weekday
        next_saturday = current + timedelta(days=days_to_saturday)
        chunk_end = min(next_saturday, end_dt)

        if chunk_end > current:
            chunks.append((current, chunk_end))

        current = chunk_end

    return chunks


def _fetch_all_ticks(conn, account_id, symbol_id, quote_type, from_ts, to_ts):
    """Fetch all ticks for a period/quote-type, handling hasMore pagination.

    The API returns up to 10,000 ticks per response, newest-first.
    When hasMore=True, older ticks remain — paginate by moving toTimestamp
    back to just before the oldest tick received.
    """
    from ctrader_open_api.messages.OpenApiMessages_pb2 import (
        ProtoOAGetTickDataReq, ProtoOAGetTickDataRes,
    )

    all_ticks = []
    current_to = to_ts

    while True:
        req = ProtoOAGetTickDataReq()
        req.ctidTraderAccountId = account_id
        req.symbolId = symbol_id
        req.type = quote_type
        req.fromTimestamp = from_ts
        req.toTimestamp = current_to

        conn.send(_PT_GET_TICKDATA_REQ, req)

        # Rate limit: 200ms between requests (API limit is 5 req/sec)
        time.sleep(0.2)

        pt, payload = conn.recv(timeout=60)
        res = _parse_response(pt, payload, _PT_GET_TICKDATA_RES, ProtoOAGetTickDataRes)

        if res.tickData:
            decoded = _decode_tick_data(res.tickData)
            all_ticks.extend(decoded)

            if res.hasMore:
                # decoded is chronological: [0]=oldest, [-1]=newest
                oldest_ts = decoded[0][0]
                new_to = oldest_ts - 1
                if new_to >= current_to:
                    # Safety: avoid infinite loop if timestamps don't advance
                    break
                current_to = new_to
                continue

        break

    # Sort chronologically (pages were fetched newest-to-oldest)
    all_ticks.sort(key=lambda t: t[0])
    return all_ticks


def _download_ticks_sync(
    symbol: str,
    start_date: str,
    end_date: str,
    output_dir: Optional[str] = None,
    account_id: Optional[int] = None,
    progress_callback: Optional[Callable[[str], None]] = None,
) -> dict:
    """
    Synchronous tick download from cTrader Open API.

    Dates should be in YYYY-MM-DD format.
    Returns dict with status, path, tick_count, file_size_mb, price_range.
    """
    try:
        from ctrader_open_api.messages.OpenApiMessages_pb2 import (
            ProtoOAApplicationAuthReq, ProtoOAApplicationAuthRes,
            ProtoOAAccountAuthReq, ProtoOAAccountAuthRes,
            ProtoOASymbolsListReq, ProtoOASymbolsListRes,
        )
        from ctrader_open_api.messages.OpenApiModelMessages_pb2 import ProtoOAQuoteType
        import pandas as pd
    except ImportError as e:
        return {
            "status": "error",
            "message": f"Missing dependency: {e}. Install with: pip install ctrader-open-api pandas",
        }

    # Load credentials from settings (reads .env via pydantic-settings)
    from api.config import get_settings
    settings = get_settings()

    client_id = settings.ctrader_client_id
    client_secret = settings.ctrader_client_secret
    access_token = settings.ctrader_access_token
    env_account_id = settings.ctrader_account_id
    host_type = settings.ctrader_host_type.lower()

    ctid_account = account_id or (int(env_account_id) if env_account_id else 0)

    if not all([client_id, client_secret, access_token, ctid_account]):
        return {
            "status": "error",
            "message": (
                "Missing cTrader credentials. Set BT_CTRADER_CLIENT_ID, "
                "BT_CTRADER_CLIENT_SECRET, BT_CTRADER_ACCESS_TOKEN, and "
                "BT_CTRADER_ACCOUNT_ID in your .env file."
            ),
        }

    host = LIVE_HOST if host_type == "live" else DEMO_HOST
    conn = None

    try:
        if progress_callback:
            progress_callback("Connecting to cTrader Open API...")
        conn = _CTraderConnection(host, PROTO_PORT)

        # --- Application auth ---
        if progress_callback:
            progress_callback("Authenticating application...")
        app_auth = ProtoOAApplicationAuthReq()
        app_auth.clientId = client_id
        app_auth.clientSecret = client_secret
        conn.send(_PT_APP_AUTH_REQ, app_auth)

        pt, payload = conn.recv()
        _parse_response(pt, payload, _PT_APP_AUTH_RES, ProtoOAApplicationAuthRes)

        # --- Account auth ---
        if progress_callback:
            progress_callback("Authenticating account...")
        acc_auth = ProtoOAAccountAuthReq()
        acc_auth.ctidTraderAccountId = ctid_account
        acc_auth.accessToken = access_token
        conn.send(_PT_ACCOUNT_AUTH_REQ, acc_auth)

        pt, payload = conn.recv()
        _parse_response(pt, payload, _PT_ACCOUNT_AUTH_RES, ProtoOAAccountAuthRes)

        # --- Resolve symbol name → symbolId ---
        if progress_callback:
            progress_callback(f"Resolving symbol {symbol}...")
        sym_req = ProtoOASymbolsListReq()
        sym_req.ctidTraderAccountId = ctid_account
        conn.send(_PT_SYMBOLS_LIST_REQ, sym_req)

        pt, payload = conn.recv(timeout=60)
        sym_res = _parse_response(pt, payload, _PT_SYMBOLS_LIST_RES, ProtoOASymbolsListRes)

        symbol_id = None
        for s in sym_res.symbol:
            if s.symbolName == symbol:
                symbol_id = s.symbolId
                break

        if symbol_id is None:
            return {
                "status": "error",
                "message": f"Symbol '{symbol}' not found in cTrader account",
            }

        # --- Download ticks in weekly chunks ---
        start_dt = datetime.strptime(start_date, "%Y-%m-%d").replace(tzinfo=timezone.utc)
        end_dt = datetime.strptime(end_date, "%Y-%m-%d").replace(tzinfo=timezone.utc)

        chunks = _week_chunks(start_dt, end_dt)
        total_chunks = len(chunks)

        all_bid_ticks = []
        all_ask_ticks = []

        for i, (chunk_start, chunk_end) in enumerate(chunks):
            from_ts = int(chunk_start.timestamp() * 1000)
            to_ts = int(chunk_end.timestamp() * 1000)

            if progress_callback:
                progress_callback(
                    f"Downloading chunk {i + 1}/{total_chunks}: "
                    f"{chunk_start.strftime('%Y-%m-%d')} to {chunk_end.strftime('%Y-%m-%d')}..."
                )

            # BID ticks
            bid_ticks = _fetch_all_ticks(
                conn, ctid_account, symbol_id, ProtoOAQuoteType.BID, from_ts, to_ts,
            )
            all_bid_ticks.extend(bid_ticks)

            # ASK ticks
            ask_ticks = _fetch_all_ticks(
                conn, ctid_account, symbol_id, ProtoOAQuoteType.ASK, from_ts, to_ts,
            )
            all_ask_ticks.extend(ask_ticks)

        conn.close()
        conn = None

        if not all_bid_ticks and not all_ask_ticks:
            return {
                "status": "error",
                "message": f"No tick data returned for {symbol} in {start_date} to {end_date}",
            }

        # --- Merge bid/ask and write CSV ---
        if progress_callback:
            progress_callback("Merging bid/ask ticks and writing CSV...")

        bid_df = pd.DataFrame(all_bid_ticks, columns=["ts_ms", "bid"])
        ask_df = pd.DataFrame(all_ask_ticks, columns=["ts_ms", "ask"])

        # Outer join on timestamp, forward-fill missing values
        merged = pd.merge(bid_df, ask_df, on="ts_ms", how="outer").sort_values("ts_ms")
        merged["bid"] = merged["bid"].ffill()
        merged["ask"] = merged["ask"].ffill()
        merged = merged.dropna(subset=["bid", "ask"])

        # Format timestamps: YYYY.MM.DD HH:MM:SS.mmm
        merged["Timestamp"] = pd.to_datetime(merged["ts_ms"], unit="ms", utc=True)
        merged["Timestamp"] = (
            merged["Timestamp"].dt.strftime("%Y.%m.%d %H:%M:%S.")
            + (merged["ts_ms"] % 1000).astype(int).astype(str).str.zfill(3)
        )

        output_df = pd.DataFrame({
            "Timestamp": merged["Timestamp"],
            "Bid": merged["bid"].round(5),
            "Ask": merged["ask"].round(5),
            "Volume": 0,
            "Flags": 0,
        })

        # Write tab-separated CSV (C++ engine format)
        if output_dir is None:
            output_dir = str(_project_root / "validation" / symbol)

        os.makedirs(output_dir, exist_ok=True)
        output_path = os.path.join(output_dir, f"{symbol}_TICKS_CTRADER.csv")

        output_df.to_csv(output_path, sep="\t", index=False)
        file_size_mb = round(os.path.getsize(output_path) / (1024 * 1024), 1)
        tick_count = len(output_df)

        return {
            "status": "success",
            "symbol": symbol,
            "path": output_path,
            "tick_count": tick_count,
            "file_size_mb": file_size_mb,
            "start_date": start_date,
            "end_date": end_date,
            "price_range": {
                "bid_min": float(output_df["Bid"].min()),
                "bid_max": float(output_df["Bid"].max()),
                "ask_min": float(output_df["Ask"].min()),
                "ask_max": float(output_df["Ask"].max()),
            },
        }

    except Exception as e:
        logger.error(f"cTrader tick download error: {e}", exc_info=True)
        return {"status": "error", "message": str(e)}

    finally:
        if conn:
            conn.close()


async def download_ticks(
    symbol: str,
    start_date: str,
    end_date: str,
    output_dir: Optional[str] = None,
    account_id: Optional[int] = None,
) -> dict:
    """Async wrapper — runs blocking download in thread pool."""
    logger.info(f"Starting cTrader tick download: {symbol} {start_date} to {end_date}")

    result = await asyncio.to_thread(
        _download_ticks_sync,
        symbol,
        start_date,
        end_date,
        output_dir,
        account_id,
    )

    if result["status"] == "success":
        logger.info(
            f"Downloaded {result['tick_count']:,} ticks for {symbol} "
            f"({result['file_size_mb']} MB) from cTrader"
        )
    else:
        logger.error(f"cTrader tick download failed: {result['message']}")

    return result


def is_ctrader_configured() -> bool:
    """Check if cTrader Open API credentials are set."""
    from api.config import get_settings
    settings = get_settings()
    return all([
        settings.ctrader_client_id,
        settings.ctrader_client_secret,
        settings.ctrader_access_token,
        settings.ctrader_account_id,
    ])


def _list_symbols_sync() -> dict:
    """Fetch all available symbol names from cTrader Open API (synchronous)."""
    if not is_ctrader_configured():
        return {"status": "success", "symbols": []}

    try:
        from ctrader_open_api.messages.OpenApiMessages_pb2 import (
            ProtoOAApplicationAuthReq, ProtoOAApplicationAuthRes,
            ProtoOAAccountAuthReq, ProtoOAAccountAuthRes,
            ProtoOASymbolsListReq, ProtoOASymbolsListRes,
        )
    except ImportError as e:
        return {"status": "error", "message": f"Missing dependency: {e}"}

    from api.config import get_settings
    settings = get_settings()

    client_id = settings.ctrader_client_id
    client_secret = settings.ctrader_client_secret
    access_token = settings.ctrader_access_token
    ctid_account = int(settings.ctrader_account_id)
    host = LIVE_HOST if settings.ctrader_host_type.lower() == "live" else DEMO_HOST

    conn = None
    try:
        conn = _CTraderConnection(host, PROTO_PORT)

        # Application auth
        app_auth = ProtoOAApplicationAuthReq()
        app_auth.clientId = client_id
        app_auth.clientSecret = client_secret
        conn.send(_PT_APP_AUTH_REQ, app_auth)
        pt, payload = conn.recv()
        _parse_response(pt, payload, _PT_APP_AUTH_RES, ProtoOAApplicationAuthRes)

        # Account auth
        acc_auth = ProtoOAAccountAuthReq()
        acc_auth.ctidTraderAccountId = ctid_account
        acc_auth.accessToken = access_token
        conn.send(_PT_ACCOUNT_AUTH_REQ, acc_auth)
        pt, payload = conn.recv()
        _parse_response(pt, payload, _PT_ACCOUNT_AUTH_RES, ProtoOAAccountAuthRes)

        # Fetch symbols list
        sym_req = ProtoOASymbolsListReq()
        sym_req.ctidTraderAccountId = ctid_account
        conn.send(_PT_SYMBOLS_LIST_REQ, sym_req)
        pt, payload = conn.recv(timeout=60)
        sym_res = _parse_response(pt, payload, _PT_SYMBOLS_LIST_RES, ProtoOASymbolsListRes)

        symbols = sorted([s.symbolName for s in sym_res.symbol if s.symbolName])
        return {"status": "success", "symbols": symbols}

    except Exception as e:
        logger.error(f"cTrader list_symbols error: {e}", exc_info=True)
        return {"status": "error", "message": str(e)}
    finally:
        if conn:
            conn.close()


async def list_symbols() -> dict:
    """Async wrapper for listing cTrader symbols."""
    return await asyncio.to_thread(_list_symbols_sync)
