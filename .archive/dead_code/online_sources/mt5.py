from datetime import datetime
import matplotlib.pyplot as plt
import pandas as pd
from pandas.plotting import register_matplotlib_converters

register_matplotlib_converters()
import MetaTrader5 as mt5

# connect to MetaTrader 5
#if not mt5.initialize():
if not mt5.initialize(login=000000, server="BrokerServer", password="REDACTED"):
    print("initialize() failed, error code =", mt5.last_error())
    mt5.shutdown()
    quit()

# print("accont info: ")
# account_info = mt5.account_info()
# if account_info != None:
#     # display trading account data 'as is'
#     print(account_info)
#     # display trading account data in the form of a dictionary
#     print("Show account_info()._asdict():")
#     account_info_dict = mt5.account_info()._asdict()
#     for prop in account_info_dict:
#         print("  {}={}".format(prop, account_info_dict[prop]))
#     print()
#
#     # convert the dictionary into DataFrame and print
#     df = pd.DataFrame(list(account_info_dict.items()), columns=['property', 'value'])
#     print("account_info() as dataframe:")
#     print(df)
#
# # request connection status and parameters
# print("terminal info: ")
# terminal_info=mt5.terminal_info()
# if terminal_info!=None:
#     # display the terminal data 'as is'
#     print(terminal_info)
#     # display data in the form of a list
#     print("Show terminal_info()._asdict():")
#     terminal_info_dict = mt5.terminal_info()._asdict()
#     for prop in terminal_info_dict:
#         print("  {}={}".format(prop, terminal_info_dict[prop]))
#     print()
#    # convert the dictionary into DataFrame and print
#     df=pd.DataFrame(list(terminal_info_dict.items()),columns=['property','value'])
#     print("terminal_info() as dataframe:")
#     print(df)
#
# # get data on MetaTrader 5 version
# print("version info: ")
# print(mt5.version())
#
# print("symbols info: ")
# symbols=mt5.symbols_total()
# if symbols>0:
#     print("Total symbols =",symbols)
# else:
#     print("symbols not found")
#
# print("XAUUSD info: ")
# symbol_info=mt5.symbol_info("XAUUSD")
# if symbol_info!=None:
#     # display the terminal data 'as is'
#     print(symbol_info)
#     print("XAUUSD: spread =",symbol_info.spread,"  digits =",symbol_info.digits)
#     # display symbol properties as a list
#     print("Show symbol_info(\"XAUUSD\")._asdict():")
#     symbol_info_dict = mt5.symbol_info("XAUUSD")._asdict()
#     for prop in symbol_info_dict:
#         print("  {}={}".format(prop, symbol_info_dict[prop]))

# request 1000 ticks from XAUUSD
xauusd_ticks = mt5.copy_ticks_range("XAUUSD", datetime(2023, 1, 1, 0, 0, 0), datetime(2024, 1, 1, 1, 0, 0), mt5.COPY_TICKS_ALL)

# get bars from different symbols in a number of ways
#xauusd_rates = mt5.copy_rates_from("XAUUSD", mt5.TIMEFRAME_M1, datetime(2024, 1, 1, 1), 1000)
#eurgbp_rates = mt5.copy_rates_from_pos("XAUUSD", mt5.TIMEFRAME_M1, 0, 1000)
#eurcad_rates = mt5.copy_rates_range("XAUUSD", mt5.TIMEFRAME_M1, datetime(2020, 1, 27, 13), datetime(2020, 1, 28, 13))

# shut down connection to MetaTrader 5
if not mt5.shutdown():
    print("shutdown() failed, error code =", mt5.last_error())

# DATA
print('xauusd_ticks(', len(xauusd_ticks), ')')
for val in xauusd_ticks[:10]: print(val)

# # create DataFrame out of the obtained data
ticks_frame = pd.DataFrame(xauusd_ticks)
# # convert time in seconds into the datetime format
ticks_frame['time'] = pd.to_datetime(ticks_frame['time'], unit='s')
#
# # display data
print("\nDisplay dataframe with ticks")
print(ticks_frame['time'].head(10))

ticks_frame.to_csv("xauusd.csv", sep='\t', encoding='utf-8')

print(ticks_frame.time.dt.day)

#print('xauusd_rates(', len(xauusd_rates), ')')
#for val in xauusd_rates[:10]: print(val)

# PLOT
# create DataFrame out of the obtained data
#ticks_frame = pd.DataFrame(xauusd_ticks)
#print(ticks_frame)
# convert time in seconds into the datetime format
#ticks_frame['time'] = pd.to_datetime(ticks_frame['time'], unit='s')

# # display ticks on the chart
# plt.plot(ticks_frame['time'], ticks_frame['ask'], 'r-', label='ask')
# plt.plot(ticks_frame['time'], ticks_frame['bid'], 'b-', label='bid')
# # display the legends
# plt.legend(loc='upper left')
# # add the header
# plt.title('XAUUSD ticks')
# # display the chart
# plt.show()

# slice into days
# to eliminate artificial overnight jumps, as nothing can be done about them

