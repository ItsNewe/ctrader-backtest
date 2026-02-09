# -*- coding: utf-8 -*-
'''
Created on 2015/01/25

@author: samuraitaiga
'''
import logging
import os
from metatrader.mt4 import DEFAULT_MT4_NAME
from metatrader.mt4 import get_mt4
#from __builtin__ import str

class BackTest(object):
    """
    Attributes:
      ea_name(string): ea name
      param(dict): ea parameter
      symbol(string): currency symbol. e.g.: USDJPY
      from_date(datetime.datetime): backtest from date
      to_date(datetime.datetime): backtest to date
      model(int): backtest model 
        0: Every tick
        1: Control points
        2: Open prices only
      spread(int): spread
      optimization(bool): optimization flag. optimization is enabled if True
      replace_report(bool): replace report flag. replace report is enabled if True

    """
    def __init__(self, ea_name, param, symbol, period, from_date, to_date, model=0, spread=5, replace_repot=1):
        self.ea_name = ea_name
        self.param = param
        self.symbol = symbol
        self.period = period
        self.from_date = from_date
        self.to_date = to_date
        self.model = model
        self.spread = spread
        self.replace_report = replace_repot

    def _prepare(self, alias=DEFAULT_MT4_NAME):
        """
        Notes:
          create backtest config file and parameter file
        """
        self._create_conf(alias=alias)
        self._create_param(alias=alias)

    def _create_conf(self, alias=DEFAULT_MT4_NAME):
        """
        Notes:
          create config file(.conf) which is used parameter of terminal.exe
          in %APPDATA%\\MetaQuotes\\Terminal\\<UUID>\\tester
          
          file contents goes to 
            TestExpert=SampleEA
            TestExpertParameters=SampleEA.set
            TestSymbol=USDJPY
            TestPeriod=M5
            TestModel=0
            TestSpread=5
            TestOptimization=true
            TestDateEnable=true
            TestFromDate=2014.09.01
            TestToDate=2015.01.05
            TestReport=SampleEA
            TestReplaceReport=false
            TestShutdownTerminal=true
        """

        mt4 = get_mt4(alias=alias)
        conf_file = os.path.join(mt4.appdata_path, 'tester', '%s.ini' % self.ea_name)

        # shutdown_terminal must be True.
        # If false, popen don't end and backtest report analyze don't start.
        shutdown_terminal = 1

        with open(conf_file, 'w') as fp:
            fp.write('\n[Common]\n')
            fp.write('Environment=F008C6283604CDCF1C737BF85BF17059D5EDEB4D1BE9D3D5186F76F341D71700BCD4F3551EECA7A9489F4FCC7F155039\n')
            fp.write('Login=17520063\n')
            #fp.write('Password=rti2odow\n')
            fp.write('ProxyEnable=0\n')
            fp.write('ProxyType=0\n')
            fp.write('ProxyAddress=\n')
            fp.write('ProxyAuth=\n')
            #fp.write('ProxyLogin=\n')
            #fp.write('ProxyPassword=\n')
            #fp.write('KeepPrivate=1\n')
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
            fp.write('[Experts]\n')
            #fp.write('AllowLiveTrading=0\n')
            #fp.write('AllowDllImport=0\n')
            #fp.write('Enabled=1\n')
            #fp.write('Account=0\n')
            #fp.write('Profile=0\n')
            #fp.write('[Objects]\n')
            #fp.write('ShowPropertiesOnCreate=0\n')
            #fp.write('SelectOneClick=0\n')
            #fp.write('MagnetSens=10\n')
            #fp.write('[StartUp]\n')
            #fp.write('Symbol=XAUUSD\n')
            #fp.write('Period=M1\n')
            fp.write('[Tester]\n')
            fp.write('Expert=%s\n' % self.ea_name)
            fp.write('ExpertParameters=%s.set\n' % self.ea_name)
            fp.write('Symbol=%s\n' % self.symbol)
            fp.write('Period=%s\n' % self.period)
            fp.write('Login=17520063\n')
            fp.write('Model=%s\n' % self.model)
            fp.write('ExecutionMode=0\n')
            fp.write('Optimization=%s\n' % self.optimization)
            fp.write('OptimizationCriterion=0\n')
            fp.write('FromDate=%s\n' % self.from_date.strftime('%Y.%m.%d'))
            fp.write('ToDate=%s\n' % self.to_date.strftime('%Y.%m.%d'))
            fp.write('ForwardMode=0\n')
            #fp.write('ForwardDate')
            fp.write('Report=%s\n' % self.ea_name)
            fp.write('ReplaceReport=%s\n' % self.replace_report)
            fp.write('ShutdownTerminal=%s\n' % shutdown_terminal)
            fp.write('Deposit=3000\n')
            fp.write('Currency=USD\n')
            fp.write('Leverage=1:100\n')
            fp.write('UseLocal=1\n')
            fp.write('UseRemote=1\n')
            fp.write('UseCloud=0\n')
            fp.write('Visual=0\n')
            #fp.write('Port=\n')

    def _create_param(self, alias=DEFAULT_MT4_NAME):
        """
        Notes:
          create ea parameter file(.set) in %APPDATA%\\MetaQuotes\\Terminal\\<UUID>\\tester
        Args:
          ea_name(string): ea name
        """
        mt4 = get_mt4(alias=alias)
        param_file = os.path.join(mt4.appdata_path, 'MQL5\\Profiles\\Tester\\', '%s_.set' % self.ea_name)

        with open(param_file, 'w') as fp:
            for k in self.param:
                values = self.param[k].copy()
                value = values.pop('value')
                #fp.write('%s=%s\n' % (k, value))
                if 'max' in values and 'step' in values and 'min' in values:
                    opt = 'Y'
                    min = values.pop('min')
                    step = values.pop('step')
                    max = values.pop('max')
                    #fp.write('%s = %s  %s  %s  %s  &s\n' % (k, value, min, step, max, opt))
                    fp.write('{}={}||{}||{}||{}||{}\n'.format(k, value, min, step, max, opt))
                else:
                    # if this value won't be optimized, write unused dummy data for same format.
                    opt = 'N'
                    fp.write('{}={}\n' .format(k, value))


    def _get_conf_abs_path(self, alias=DEFAULT_MT4_NAME):
        mt4 = get_mt4(alias=alias)
        conf_file = os.path.join(mt4.appdata_path, 'tester', '%s.conf' % self.ea_name)
        return conf_file

    def run(self, alias=DEFAULT_MT4_NAME):
        """
        Notes:
          run backtest
        """
        from metatrader.report import BacktestReport

        #self.optimization = False
        self.optimization = 1

        self._prepare(alias=alias)
        bt_conf = self._get_conf_abs_path(alias=alias)
    
        mt4 = get_mt4(alias=alias)
        mt4.run(self.ea_name, conf=bt_conf)
    
        ret = BacktestReport(self)
        return ret

    def optimize(self, alias=DEFAULT_MT4_NAME):
        """
        """
        from metatrader.report import OptimizationReport

        self.optimization = True
        self._prepare(alias=alias)
        bt_conf = self._get_conf_abs_path(alias=alias)
    
        mt4 = get_mt4(alias=alias)
        mt4.run(self.ea_name, conf=bt_conf)
        
        ret = OptimizationReport(self)
        return ret


def load_from_file(dsl_file):
    pass
