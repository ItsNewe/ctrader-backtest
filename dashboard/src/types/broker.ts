export interface BrokerConnectRequest {
  broker: string;
  account_type: string;
  account_id: string;
  leverage: number;
  account_currency: string;
  mt5_path?: string;
  password?: string;
  server?: string;
}

export interface InstrumentSpec {
  symbol: string;
  broker: string;
  contract_size: number;
  margin_requirement: number;
  pip_value: number;
  pip_size: number;
  commission_per_lot: number;
  swap_buy: number;
  swap_sell: number;
  min_volume: number;
  max_volume: number;
  fetched_at: string;
}

export interface AccountInfo {
  balance: number;
  equity: number;
  free_margin: number;
  leverage: number;
  currency: string;
}
