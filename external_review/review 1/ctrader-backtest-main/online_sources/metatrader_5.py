#!/usr/bin/env python
# -*- coding: utf-8 -*-

from datetime import datetime, timedelta
import os
import subprocess
import math

# specify testing period
from_date = datetime(2019, 1, 1)
to_date = datetime(2019, 1, 9)
daily_resolution = True  # run separate optimizations for each day if true, or run it as one long interval
days_interval = 2

# specify the name of the expert advisor
ea_name = 'static_grid_with_direction_modify'

symbol = 'XAUUSD'  # symbol to test
period = 'M1'  # time resolution of the simulation
deposit = 1000000  # starting deposit value
model = 0  # every tick
criteria = 6  # 0 max balance, 1 max of product of balance and profitability, 2 product of the balance and expected payoff,
# 3 maximum of (100% - Drawdown)*Balance, 4 product of balance and recovery factor, 5 product of  balance and  Sharpe Ratio,
# 6 custom optimization criterion from the OnTester() function in the Expert Advisor
optimization = 2  # (0 disabled, 1 complete, 2 genetic, 3 All symbols selected in Market Watch
forward = 0  # # 0 no, 1 1/2, 2 1/3, 3 1/4, 4 custom set in forwarddate
use_local = 1  # use local machine resources
use_remote = 1  # use local network

ntpath = 'C:\\Program Files\\MetaTrader 5'
DEFAULT_MT5_NAME = 'default'
MT5_EXE = 'terminal64.exe'

prog_path = None
mt5_appdata_path = None

if os.path.exists(ntpath):
    prog_path = ntpath
    app_data = os.environ.get('AppData')
    mt5_appdata_path = os.path.join(app_data, 'MetaQuotes', 'Terminal', 'D0E8209F77C8CF37AD8BF550E51FF075')

replace_report = 1 #0  # 1
shutdown_terminal = 1

conf_file = os.path.join(mt5_appdata_path, 'tester', '%s.ini' % ea_name)
param_file = os.path.join(mt5_appdata_path, 'MQL5\\Profiles\\Tester\\', '%s.set' % ea_name)
conf = os.path.join(mt5_appdata_path, 'tester', '%s.ini' % ea_name)
prog = '%s' % os.path.join(prog_path, MT5_EXE)
conf = '/config:%s' % conf
cmd = '%s %s' % (prog, conf)

# specify default value, then min, max and step values for all optimizable parameters.
param = {
         'max_unit': {'value': 100},
         'spread_difference': {'value': 0, 'minimum': 0, 'step': 1, 'maximum': 150}, #4?
         'gridstep': {'value': 0.01},
         'stop_loss_limit': {'value': 1, 'minimum': 1, 'step': 1, 'maximum': 100000},
         'angle': {'value': 0.01, 'minimum': 0.000001, 'step': 0.000001, 'maximum': 0.001},
         'frequency': {'value': 1, 'minimum': 0.01, 'step': 0.01, 'maximum': 1},
         'steepness': {'value': 1, 'minimum': 1, 'step': 1, 'maximum': 20},
         'phase': {'value': 0, 'minimum': -1*180, 'step': 1, 'maximum': 180},
         'DC': {'value': 0.1, 'minimum': 0, 'step': 0.01, 'maximum': 1},
         'puffer': {'value': 0.1, 'minimum': 0.01, 'step': 0.01, 'maximum': 1},
         #'puffer': {'value': 1},
         'LogOptimizationReport': {'value': "true"},
         'CriterionSelectionRule': {'value': 1},
         'Criterion_01': {'value': 1},
         'CriterionValue_01': {'value': 0},
         'Criterion_02': {'value': 0},
         'CriterionValue_02': {'value': 0},
         'Criterion_03': {'value': 0},
         'CriterionValue_03': {'value': 0},
         'output_file_location_and_name': {'value': ""}
         }

days_list = []
if daily_resolution:
    date_lower = from_date - timedelta(days=1)  # needed for the proper functioning of the loop
    date_upper = date_lower + timedelta(days=days_interval)
    #days_list.append([date_lower, date_upper])
    for i in range((to_date - from_date - timedelta(days=days_interval)).days):
        flag = False
        date_lower += timedelta(days=1)
        while date_lower.weekday() > 4:  # if weekend, increase
            date_lower += timedelta(days=1)
        date_upper = date_lower + timedelta(days=days_interval)
        for d_it in range(1, days_interval+1):
            if (date_lower + timedelta(days=d_it)).weekday() > 4:
                flag = True
        if flag:
            date_upper = date_upper + timedelta(days=2)  # +2, bc of weekend days
        days_list.append([date_lower, date_upper])
else:
    days_list.append([from_date, to_date])

for day in days_list:
    print(day)
    if (day[1] > to_date):
        break
    with open(conf_file, 'w') as fp:
        fp.write('\n[Common]\n')
        fp.write('Environment=F008C6283604CDCF1C737BF85BF17059D5EDEB4D1BE9D3D5186F76F341D71700BCD4F3551EECA7A9489F4FCC7F155039\n')
        #fp.write('Login=17520063\n')
        # fp.write('Password=rti2odow\n')
        fp.write('ProxyEnable=0\n')
        fp.write('ProxyType=0\n')
        fp.write('ProxyAddress=\n')
        fp.write('ProxyAuth=\n')
        # fp.write('ProxyLogin=\n')
        # fp.write('ProxyPassword=\n')
        # fp.write('KeepPrivate=1\n')
        fp.write('NewsEnable=1\n')
        fp.write('CertInstall=0\n')
        fp.write('NewsLanguages=\n')
        fp.write('[Charts]\n')
        fp.write('ProfileLast=Default\n')
        fp.write('MaxBars=100000\n')
        fp.write('PrintColor=0\n')
        fp.write('SaveDeleted=1\n')
        fp.write('TradeLevels=1\n')
        fp.write('TradeLevelsDrag=0\n')
        fp.write('ObsoleteLasttime=1558195622\n')
        # fp.write('[Experts]\n')
        # fp.write('AllowLiveTrading=0\n')
        # fp.write('AllowDllImport=0\n')
        # fp.write('Enabled=1\n')
        # fp.write('Account=0\n')
        # fp.write('Profile=0\n')
        # fp.write('[Objects]\n')
        # fp.write('ShowPropertiesOnCreate=0\n')
        # fp.write('SelectOneClick=0\n')
        # fp.write('MagnetSens=10\n')
        # fp.write('[StartUp]\n')
        # fp.write('Symbol=XAUUSD\n')
        # fp.write('Period=M1\n')
        fp.write('[Tester]\n')
        fp.write('Expert=%s\n' % ea_name)
        fp.write('ExpertParameters=%s.set\n' % ea_name)
        fp.write('Symbol=%s\n' % symbol)
        fp.write('Period=%s\n' % period)
        fp.write('Login=17520063\n')
        fp.write('Model=%s\n' % model)
        fp.write('ExecutionMode=0\n')
        fp.write('Optimization=%s\n' % optimization)
        fp.write('OptimizationCriterion=%s\n' % criteria)
        fp.write('FromDate=%s\n' % day[0].strftime('%Y.%m.%d'))
        fp.write('ToDate=%s\n' % day[1].strftime('%Y.%m.%d'))
        fp.write('ForwardMode=%s\n' % forward)
        # fp.write('ForwardDate')
        # fp.write('Report=%s\n' % ea_name)
        fp.write('Report=1_{}_{}_{}_{}.csv\n'.format(ea_name, day[0].strftime('%Y%m%d'), day[1].strftime('%Y%m%d'),deposit))
        fp.write('ReplaceReport=%s\n' % replace_report)
        fp.write('Deposit=%s\n' % deposit)
        fp.write('Currency=USD\n')
        fp.write('Leverage=1:100\n')
        fp.write('UseLocal=%s\n' % use_local)
        fp.write('UseRemote=%s\n' % use_remote)
        fp.write('UseCloud=0\n')
        fp.write('Visual=0\n')
        # fp.write('Port=\n')
        fp.write('ShutdownTerminal=%s\n' % shutdown_terminal)

    with open(param_file, 'w') as fp:
        values1 = param.copy()
        for j in values1:
            values = values1[j].copy()
            #print(values)
            value = values.pop('value')
            if j == "output_file_location_and_name":
                opt = 'N'
                fp.write('{}=1_{}_{}_{}_{}.csv\n'.format(j, ea_name, day[0].strftime('%Y%m%d'), day[1].strftime('%Y%m%d'),deposit))
            elif 'maximum' in values and 'step' in values and 'minimum' in values:
                opt = 'Y'
                minimum = values.pop('minimum')
                step = values.pop('step')
                maximum = values.pop('maximum')
                fp.write('{}={}||{}||{}||{}||{}\n'.format(j, value, minimum, step, maximum, opt))
            else:
                # if this value won't be optimized, write unused dummy data for same format.
                opt = 'N'
                fp.write('{}={}\n'.format(j, value))

    p = subprocess.Popen(cmd)
    p.wait()
    if p.returncode == 0:
        print('cmd[%s] \n succeeded' % cmd)
    else:
        print('run mt4 with cmd[%s] failed!!' % cmd)
        raise RuntimeError('error')
