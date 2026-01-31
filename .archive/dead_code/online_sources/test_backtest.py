'''
Created on 2015/04/15

@author: Taiga
'''

from metatrader.mt4 import initizalize
from metatrader.backtest import BackTest
from datetime import datetime
import logging

def test_backtest():
    logging.basicConfig(level=logging.DEBUG)
    #initizalize('C:\\Program Files\\FXCM MetaTrader 4')
    initizalize('C:\\Program Files\\MetaTrader 5')

    from_date = datetime(2019, 1, 1)
    to_date = datetime(2019, 5, 17)

    ea_name = 'static_grid'
    param = {
             'max_unit': {'value': 0.1, 'min': 1, 'step': 1, 'max': 10},
             'spread_difference': {'value': 0.1, 'min': 1, 'step': 1, 'max': 10},
             'gridstep': {'value': 0.1, 'min': 1, 'step': 1, 'max': 10},
             'stop_loss_limit': {'value': 0.1, 'min': 1, 'step': 1, 'max': 10},
             'angle': {'value': 0.1, 'min': 1, 'step': 1, 'max': 10}
             }


    backtest = BackTest(ea_name, param, 'XAUUSD', 'M1', from_date, to_date, spread=10)

    ret = backtest.run()
    assert ret.profit == -724.34
    assert ret.profit_factor == 0.88
    assert ret.expected_payoff == -0.59
    assert ret.max_drawdown == 1267.33
    assert ret.max_drawdown_rate == 12.55
    assert ret.gross_profit == 5368.83
    assert ret.gross_loss == -6093.17
    assert ret.total_trades == 1232
    assert ret.largest_profit_trade == 155.62
    assert ret.largest_loss_trade == -87.78
    assert ret.average_profit_trade == 18.90
    assert ret.average_loss_trade == -6.43
    assert ret.modeling_quality_percentage == 90.0
    assert ret.max_consecutive_profit == 270.45
    assert ret.max_consecutive_profit_count == 3
    assert ret.max_consecutive_loss == -192.53
    assert ret.max_consecutive_loss_count == 30
    assert ret.max_consecutive_wins_count == 4
    assert ret.max_consecutive_wins_profit == 44.31
    assert ret.max_consecutive_losses_count ==30
    assert ret.max_consecutive_losses_loss == -192.53
    assert ret.profit_trades == 284
    assert ret.profit_trades_rate == 23.05
    assert ret.loss_trades == 948
    assert ret.loss_trades_rate == 76.95
    assert ret.ave_consecutive_wins == 1
    assert ret.ave_consecutive_losses == 4
    assert ret.short_positions == 549
    assert ret.short_positions_rate == 20.95
    assert ret.long_positions == 683
    assert ret.long_positions_rate == 24.74
