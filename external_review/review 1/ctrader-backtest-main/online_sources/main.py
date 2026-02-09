# !/usr/bin/env python
# -*- coding: utf-8 -*-

from datetime import datetime, timedelta
import os
import subprocess
import math
import time
import shlex

if __name__ == '__main__':

    file_names = ["clgorithm (2)", "ilgorithm (2)", "ilgorithm (3)", "klgorithm (1)", "klgorithm (3)", "maybe - clgorithm (2)", "maybe - dlgorithm (1)", "no - ilgorithm (1)"]

    for name in file_names:
        print(name)

        # specify testing period
        from_date = datetime(2022, 1, 1)
        to_date = datetime(2022, 12, 31)

        # specify the name of the expert advisor
        ea_name = name #'clgorithm (2)'

        symbol = 'XAUUSDb'  # symbol to test
        period = 'M1'  # time resolution of the simulation
        deposit = 100000  # starting deposit value
        model = 4  # every tick based on real ticks
        criteria = 4  # 0 max balance, 1 max of product of balance and profitability, 2 product of the balance and expected payoff,
        # 3 maximum of (100% - Drawdown)*Balance, 4 product of balance and recovery factor, 5 product of  balance and  Sharpe Ratio,
        # 6 custom optimization criterion from the OnTester() function in the Expert Advisor
        optimization = 1  # (0 disabled, 1 complete, 2 genetic, 3 All symbols selected in Market Watch
        forward = 0  # # 0 no, 1 1/2, 2 1/3, 3 1/4, 4 custom set in forwarddate
        use_local = 1  # use local machine resources
        use_remote = 0  # use local network

        ntpath = 'C:\\Program Files\\AMarkets - MetaTrader 5'
        DEFAULT_MT5_NAME = 'default'
        MT5_EXE = 'terminal64.exe'

        prog_path = None
        mt5_appdata_path = None

        if os.path.exists(ntpath):
            prog_path = ntpath
            app_data = os.environ.get('AppData')
            mt5_appdata_path = os.path.join(app_data, 'MetaQuotes', 'Terminal', 'D0694EE1816B0E3A2D24B3606A663CE8')

        replace_report = 0  # 0  # 1
        shutdown_terminal = 1

        conf_file = os.path.join(mt5_appdata_path, 'tester', '%s.ini' % ea_name)
        param_file = os.path.join(mt5_appdata_path, 'MQL5\\Profiles\\Tester\\', '%s.set' % ea_name)
        conf = os.path.join(mt5_appdata_path, 'Tester', '%s.ini' % ea_name)
        prog = '%s' % os.path.join(prog_path, MT5_EXE)
        conf = '/config:%s' % conf
        cmd = '%s %s' % (prog, conf)

        # specify default value, then min, max and step values for all optimizable parameters.
        param = {
            'close_method_1': {'value': 0, 'minimum': 0, 'step': 1, 'maximum': 13},
            'close_method_2': {'value': 0, 'minimum': 0, 'step': 1, 'maximum': 13},
            'close_method_3': {'value': 0, 'minimum': 0, 'step': 1, 'maximum': 13},
            'close_method_4': {'value': 0, 'minimum': 0, 'step': 1, 'maximum': 13},
            'magical_extension': {'value': "b"},
            'commission': {'value': 0.05}
        }

        days_list = []
        days_list.append([from_date, to_date])

        for day in days_list:
            print(day)
            if (day[1] > to_date):
                break
            with open(conf_file, 'w') as fp:
                fp.write('\n[Common]\n')
                fp.write(
                    'Environment=F008C6283604CDCF1C737BF85BF17059D5EDEB4D1BE9D3D5186F76F341D71700BCD4F3551EECA7A9489F4FCC7F155039\n')
                #fp.write('Login=2118976\n')
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
                #fp.write('Login=2118976\n')
                fp.write('Model=%s\n' % model)
                fp.write('ExecutionMode=0\n')
                fp.write('Optimization=%s\n' % optimization)
                fp.write('OptimizationCriterion=%s\n' % criteria)
                fp.write('FromDate=%s\n' % day[0].strftime('%Y.%m.%d'))
                fp.write('ToDate=%s\n' % day[1].strftime('%Y.%m.%d'))
                fp.write('ForwardMode=%s\n' % forward)
                # fp.write('ForwardDate')
                # fp.write('Report=%s\n' % ea_name)
                fp.write('Report=1_{}_{}_{}_{}.csv\n'.format(ea_name, day[0].strftime('%Y%m%d'), day[1].strftime('%Y%m%d'),
                                                             deposit))
                fp.write('ReplaceReport=%s\n' % replace_report)
                fp.write('Deposit=%s\n' % deposit)
                fp.write('Currency=EUR\n')
                fp.write('Leverage=1:200\n')
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
                    # print(values)
                    value = values.pop('value')
                    if j == "output_file_location_and_name":
                        opt = 'N'
                        fp.write('{}=1_{}_{}_{}_{}.csv\n'.format(j, ea_name, day[0].strftime('%Y%m%d'),
                                                                 day[1].strftime('%Y%m%d'), deposit))
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
